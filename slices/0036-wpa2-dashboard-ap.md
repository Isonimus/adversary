---
id: '0036'
title: "WPA2-protect the dashboard SoftAP with an auto-generated per-device key"
type: slice
status: accepted
date: 2026-10-01
supersedes: []
superseded_by: []
---

## Goal

The dashboard SoftAP is started open — no link-layer encryption
([`server_manager.cpp:117`](../src/modules/server/server_manager.cpp)):

```cpp
// Use device name as SSID, no password for open AP
if (!WiFi.softAP(settings.system.deviceName, nullptr)) {
```

Everything the operator does over the dashboard crosses the air in cleartext to anyone in
RF range: the files being browsed, captured handshakes being reviewed, and — because the
HTTP Basic-auth login (`dashboardAuthEnabled`/`dashboardPassword`) rides on top of an open
link — the `Authorization: Basic base64(user:pass)` header itself, which is trivially
decoded and replayed. Basic-auth gates *who may log in*; it does nothing for *confidentiality
on the wire*, so on an open AP it gives a false sense of security.

Surfaced by the 2026-09-19 C-level security review (P1) and re-confirmed against source
(2026-10-01): there is no setting that feeds a WPA2 key into the dashboard AP, and every
`WiFi.softAP(` call in the tree passes `nullptr`. The decided fix is to WPA2-protect the
dashboard AP (the other APs — evil-twin, karma, handshake, deauth — are *intentionally* open
and out of scope; those are attack surfaces, not the operator's management plane).

## Decisions

- **Secure by construction, not by operator action.** The AP key is auto-generated on first
  start when unset, so there is **no code path that starts the dashboard AP open**. This is
  what closes the P1 — relying on the operator to go set a key would leave the open default
  in place until they did.
- **Auto-generate doubles as migration.** Existing `adversary.conf` files have no
  `dashboardApPassword` key, so it loads empty and a key is generated on the next AP start.
  No `version`-field migration logic is added (none exists today; the field is round-tripped
  but never acted on — see slice survey), because generate-on-empty covers the upgrade
  transparently.
- **Readable 12-char key over a max-length random blob.** WPA2-PSK allows 8–63 chars. A
  12-char key from a 32-symbol unambiguous alphabet (no `O/0/I/1/l`) is ~60 bits of entropy
  and can be typed once on a phone (which then remembers it). A WiFi-join QR on the device
  screen was considered and **deferred** (no QR library is linked; net-new flash + render
  code) — logged to the LEDGER as a follow-up.
- **Fail loud on a too-short custom key.** WPA2 silently falls back to **open** for a key of
  1–7 chars. The device password-entry UI therefore rejects 1–7 chars; an empty key is the
  explicit "regenerate on next start" signal, never an open AP.
- **Device-only, not on the HTTP settings API.** Changing the AP key over the dashboard
  would drop the very client making the change, so the key is editable only on-device
  (Settings), not via `/api/settings`.
- **Both layers kept.** WPA2 encrypts the link; Basic-auth still gates login — now its
  credentials travel encrypted. Defense in depth.

## Definition of Done

- **Given** the dashboard server is started and `dashboardApPassword` is empty (fresh device
  or upgraded config)
- **When** `setupAP()` runs
- **Then** a random 12-char key is generated, persisted to `adversary.conf`, and the AP is
  brought up **WPA2-protected** with that key — and the key is stable across reboots
  (regenerated only while empty)

- **Given** the dashboard server is started and `dashboardApPassword` holds a valid key
- **When** `setupAP()` runs
- **Then** `WiFi.softAP(deviceName, key)` is used and its return value is checked; a failure
  returns `false` from `setupAP()` (no silent open fallback)

- **Given** the operator opens Settings → Dashboard Server → AP Password and enters 1–7 chars
- **When** they submit
- **Then** the entry is rejected with a toast and the stored key is unchanged (never saved
  short, which would open the AP)

- **Given** the dashboard AP is running
- **When** the operator looks at the server menu screen
- **Then** the SSID and the AP key are shown so they can join

- **Given** the on-device AP-password entry field
- **When** the operator types
- **Then** the characters are masked (boyscout: the existing dashboard-password field passes
  `isPassword=false` and renders plaintext; both are fixed to masked)

- **Given** `wifi_password` pure helper
- **When** `pio test -e native` runs
- **Then** new tests cover: output length == requested, every char in the unambiguous
  alphabet, deterministic mapping from fixed entropy, distinct output from distinct entropy,
  null-termination, and the error paths (buffer too small, entropy too short) returning false

- **Given** the cardputer build
- **When** `pio run -e cardputer` runs
- **Then** it compiles and links; Flash reported vs the slice-0035 baseline (2,466,099 B)

## Design

### New setting

`SystemSettings` gains `char dashboardApPassword[64] = "";` (63-char WPA2 max + NUL), beside
the Basic-auth trio ([`settings_manager.h:82-85`](../src/modules/storage/settings_manager.h)).
Persisted exactly like `dashboardPassword` — written only when non-empty, loaded with a `""`
default — so it is absent from older configs and defaults empty.

### Pure key generator — `src/utils/wifi_password.{h,cpp}`

```cpp
// Fills `out` with `charCount` chars drawn from an unambiguous WPA2-safe alphabet,
// mapping one entropy byte per char. Returns false (and leaves out[0]='\0') if
// outCap <= charCount or entropyLen < charCount. Pure: no RNG inside, so it is
// deterministic and native-testable; the device supplies entropy via esp_fill_random.
bool fillWifiPassword(char* out, size_t outCap, size_t charCount,
                      const uint8_t* entropy, size_t entropyLen);
```

Alphabet: `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` (32 symbols — power of two, so `byte & 31` is an
unbiased index). `kDashboardApPasswordChars = 12`.

### AP start — `server_manager.cpp` `setupAP()`

Replace the `nullptr` call with:

```cpp
auto& settings = SettingsManager::getInstance().get();
if (settings.system.dashboardApPassword[0] == '\0') {
    uint8_t entropy[config::kDashboardApPasswordChars];
    esp_fill_random(entropy, sizeof(entropy));
    if (!fillWifiPassword(settings.system.dashboardApPassword,
                          sizeof(settings.system.dashboardApPassword),
                          config::kDashboardApPasswordChars, entropy, sizeof(entropy))) {
        return false;                       // fail loud; never fall through to open
    }
    SettingsManager::getInstance().save();  // persist so the key is stable across reboots
}
if (!WiFi.softAP(settings.system.deviceName, settings.system.dashboardApPassword)) {
    return false;
}
```

### On-device UI

- **Server menu screen** ([`server_menu_screen.cpp`](../src/ui/screens/server_menu_screen.cpp)):
  once running, show SSID + AP key + join URL so the operator can join. **Caveat found on
  hardware:** server mode purges the global canvas for heap headroom
  ([`main.cpp:789`](../src/main.cpp)), after which the render loop skips every frame
  (`getBuffer()` is null) — so the screen has always frozen on its pre-start frame and a
  naive "draw the key in drawMenu" never appears. Fix: provision the key up front
  (`ServerManager::ensureApKey()`), set the running state, render the running frame to the
  canvas and push it to the LCD (`adversary_ui_render_forced()`), **then** purge. The panel
  retains the pushed frame. Heap order is unchanged — purge still precedes `start()` — so the
  no-PSRAM headroom invariant is untouched. The frame is static (the AP IP is always the
  fixed `ServerManager::kDashboardIp`, also fed to `softAPConfig`); live counters can't update
  while purged, which is no regression since they never rendered either.
- **Settings screen** ([`settings_screen.cpp`](../src/ui/screens/settings_screen.cpp)): add an
  "AP Password" action under "-- Dashboard Server --" using the existing `TextInputPopup`,
  invoked **masked** (`isPassword=true`). On submit, accept only empty (= regenerate next
  start) or 8–63 chars; otherwise toast and keep the current key.
- **Boyscout:** the existing dashboard **login** password popup is invoked with
  `isPassword=false` ([`settings_screen.cpp:528`](../src/ui/screens/settings_screen.cpp)) and
  renders plaintext while typing; flip it to `true`.

### Not touched

- `/api/settings` (`server_manager.cpp`) does not gain the AP key — device-only.
- The Basic-auth first-run guard (`handleRoot`) is unchanged; it now operates over an
  encrypted link.

## Verification

- Native: `pio test -e native` — full suite green including the new `wifi_password` tests.
- Build: `pio run -e cardputer` — compiles/links; report Flash vs 2,466,099 B (expect ~0
  delta: WPA2-PSK crypto is already linked for STA).
- Grep: `WiFi.softAP(settings.system.deviceName, nullptr)` → 0 matches.
- On-device (deferred, logged): confirm a phone sees the dashboard SSID as WPA2-locked,
  joins with the on-screen key, and the dashboard loads; confirm the key survives a reboot.

## As-built

Shipped as designed.

- **Pure generator** `src/utils/wifi_password.{h,cpp}`: `fillWifiPassword(out, outCap, charCount,
  entropy, entropyLen)` maps one entropy byte per char over the 32-symbol unambiguous alphabet
  (`byte & 31`), returns `false` and leaves `out[0]='\0'` on any misuse (null args, `charCount==0`,
  buffer too small, too little entropy) so a short/empty key can never be emitted silently.
  `kDashboardApPasswordChars = 12`. (Note: the constant lives in `utils::`, not `config::` as the
  Design sketch wrote it — it is the generator's companion, so co-locating is more cohesive.)
- **Setting** `char dashboardApPassword[64]` added to `SystemSettings`; loaded with a `""` default
  and persisted only when non-empty, mirroring `dashboardPassword` (settings_manager.h).
- **AP start** (`server_manager.cpp`): key provisioning was extracted into a public, idempotent
  `ensureApKey()` — fills the field via `esp_fill_random` + `fillWifiPassword` and `save()`s only when
  empty, returning `false` on generator failure (no open-AP fallback). `setupAP()` calls it defensively
  (so any start path is covered) and builds the fixed AP IP from the new shared constant
  `ServerManager::kDashboardIp` (`192.168.4.1`) via `IPAddress::fromString`.
- **Server menu screen**: the fix for the frozen-screen caveat (found on hardware — see Design). The
  Start handler now provisions the key (`ensureApKey()`), sets the running state, force-renders the
  running frame to the canvas and pushes it to the LCD (`adversary_ui_render_forced()`), **then** purges
  — so the panel retains SSID + key + URL while the canvas RAM is freed. Heap order unchanged (purge
  still precedes `start()`); on `start()` failure the canvas is restored and an error toast shown. The
  running `drawMenu` shows `SSID: <name>` + `kDashboardIp` on line 1 and `Key: <key>` on line 2 (static,
  since the frame is painted once before purge; live counters were dropped as they never rendered).
- **Settings screen**: new "AP Key" action under "-- Dashboard Server --", masked entry
  (`isPassword=true`, maxLen 63), accepting only empty (= regenerate next start) or 8–63 chars;
  1–7 chars is rejected with an error toast and nothing is saved. Popup wired into the input-dispatch
  and render lists. **Boyscout:** the dashboard *login* password popup was invoked with
  `isPassword=false` (plaintext while typing) — flipped to `true`.

Verification:
- Native: `pio test -e native` — **784/784** (adds 11 `test_wifi_password` cases: length, alphabet
  membership, determinism, mapping, distinct-entropy, null-term, and the four error paths + default
  sanity).
- Build: `pio run -e cardputer` **SUCCESS**, Flash **2,467,611 B** (73.8%) vs the slice-0035 baseline
  2,466,099 B = **+1,512 B** — WPA2-PSK crypto was already linked; the delta is the generator, the
  on-screen rework, and UI strings. RAM 26.5%.
- Grep: `softAP(settings.system.deviceName, nullptr)` → **0** matches; the WPA2 call is at
  `server_manager.cpp:138`.
- **On-device CONFIRMED (2026-10-01, Cardputer ADV)**: Start Server paints and holds the
  `SSID / 192.168.4.1 / Key:` frame on the LCD (previously froze blank); a phone associated to the
  WPA2 AP and reached the captive portal; the key survived a reboot — the serial showed no config
  re-save on the second start, i.e. `ensureApKey()` reused the persisted key. One pre-existing,
  non-fatal warning on server stop (`E wifi_init_default: netstack cb reg failed with 12308`); the
  canvas restored and the menu returned cleanly. Logged to the LEDGER watch list.
