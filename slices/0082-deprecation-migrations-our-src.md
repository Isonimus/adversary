---
id: '0082'
title: "Migrate the deprecated-API calls in our own src/ (ArduinoJson containsKey + ESP-IDF app-desc)"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

Close the ledger `[feature]` that tracks the deferred half of the compiler-warning backlog:
the `-Wdeprecated-declarations` set that slice-0081 deliberately left out as semantic
upstream-migration work. With the seven low-risk classes already cleared (0081), this is the
last thing standing between our own `src/` and a fully warning-clean build, which the agreed
`-Werror` ratchet (its own ADR + CI gate, next) is blocked on.

Ground truth from a forced `src/`-only recompile (2026-10-20) corrected the ledger's earlier
characterization. The ledger said "`server_manager.cpp` x14 HTTP/server API calls"; the
compiler reports no such thing. The WebServer API (`server_->send`/`on`/`sendHeader`/`arg`/
`hasArg`) is **not** deprecated and emits zero warnings. The real set is 16 physical sites,
almost entirely one uniform ArduinoJson migration plus one ESP-IDF rename:

| Class | Physical sites | Files |
|---|---|---|
| ArduinoJson `containsKey` -> `.is<T>()` | 15 | handshake_metadata.h (223,229); server_manager.cpp (454,456,460,463,467,471,476,478,481,487,489,492,495) |
| ESP-IDF `esp_ota_get_app_description()` -> `esp_app_get_description()` | 1 | server_manager.cpp (389) |

After this slice, our own `src/` emits zero `-Wdeprecated-declarations` warnings (vendored
trees excluded, as in 0081), completing the warning-clean state the `-Werror` gate requires.

## Definition of Done

- **Given** a forced `src/`-only recompile (`rm -rf .pio/build/cardputer/src && pio run -e
  cardputer`) currently emits 16 `-Wdeprecated-declarations` sites in our own `src/` (15
  ArduinoJson `containsKey`, 1 ESP-IDF `esp_ota_get_app_description`)
- **When** this slice's migrations are applied and the same recompile is run
- **Then** our own `src/` emits **zero** `-Wdeprecated-declarations` warnings (vendored trees
  excluded), and no new warning of any other class is introduced.

- **Given** the settings POST handler (`PUT/POST /api/settings`, auth-gated) that today
  guards each field with a presence-only `containsKey` and then reads it with the wrong-type
  path unchecked, so an authenticated client POSTing a mistyped string field (e.g.
  `{"system":{"deviceName":123}}`) reaches `strncpy(dest, <nullptr>, 32)` and crashes
- **When** the `containsKey` guards are migrated to type-aware `is<T>()` checks matching each
  field's read (`is<const char*>()` for the `strncpy` string fields, `is<bool>()`,
  `is<int>()`, `is<JsonObject>()` for the nested objects)
- **Then** a mistyped field is rejected (its block skipped) instead of crashing the device,
  a well-typed settings POST still applies every field exactly as before, and an absent field
  is still skipped exactly as before.

- **Given** the `/api/info` endpoint that reports the running firmware version
- **When** `esp_ota_get_app_description()->version` is replaced by
  `esp_app_get_description()->version` (identical `esp_app_desc_t` return; ESP-IDF rename only)
- **Then** `/api/info` still returns the same `version` string.

- **Given** the native unit suite
- **When** `pio test -e native` runs after the changes
- **Then** it still reports 844/844 (the migrated lines are ESP32-gated in
  `handshake_metadata.h` and native-excluded in `server_manager.cpp`, so native compiles the
  surrounding storage files but not the changed lines; the two device builds are their proof).

## Design

### What exists, and what this reuses

- **The migration is one upstream API deprecation, not fourteen.** ArduinoJson 7.4.3
  (`V743PB22`) deprecated `JsonDocument::containsKey` / `JsonObject::containsKey` in favour of
  `obj[key].is<T>()`. All 15 `containsKey` sites are the same call; the only per-site decision
  is which `T`. There is no WebServer-API migration here (that was a ledger mischaracterization,
  corrected above from the build log).
- **`containsKey` -> `is<T>()` is a semantic choice, and type-aware was chosen over faithful.**
  `containsKey("k")` means "key present"; its pure-presence equivalent is `!obj["k"].isNull()`.
  The type-aware `is<T>()` means "present **and** of type T", which is a stricter gate. We chose
  type-aware deliberately (operator decision, 2026-10-20) because at the string-field sites it
  is the root-cause fix for a latent crash: `strncpy(settings.system.deviceName,
  system["deviceName"], 32)` runs after only a presence check, and ArduinoJson converts a
  non-string variant to a null `const char*`, so a mistyped field feeds `strncpy` a `nullptr`
  (UB / crash) on an auth-gated but network-facing endpoint. `is<const char*>()` rejects the
  mistyped field instead. This is the fail-loud / boyscout fix (global quality bar), not just
  warning silencing. The cost (per-site type selection + settings-endpoint testing) is work the
  handler wanted regardless.
- **Per-site type is read-faithful.** Each guard's `T` matches how the field is then read, so a
  well-typed payload behaves identically and only a mistyped one changes (now rejected):

  | Site | Field | Read as | New guard |
  |---|---|---|---|
  | server_manager.cpp:454 | `system` | `JsonObject` | `doc["system"].is<JsonObject>()` |
  | server_manager.cpp:456 | `deviceName` | `strncpy` (string) | `system["deviceName"].is<const char*>()` |
  | server_manager.cpp:460 | `dashboardAuthEnabled` | `bool` | `system["dashboardAuthEnabled"].is<bool>()` |
  | server_manager.cpp:463 | `dashboardUsername` | `strncpy` (string) | `system["dashboardUsername"].is<const char*>()` |
  | server_manager.cpp:467 | `dashboardPassword` | `strncpy` (string) | `system["dashboardPassword"].is<const char*>()` |
  | server_manager.cpp:471 | `theme` | `as<uint8_t>()` | `system["theme"].is<int>()` |
  | server_manager.cpp:476 | `wireless` | `JsonObject` | `doc["wireless"].is<JsonObject>()` |
  | server_manager.cpp:478 | `karmaChannel` | `uint8_t` assign | `wireless["karmaChannel"].is<int>()` |
  | server_manager.cpp:481 | `bleName` | `strncpy` (string) | `wireless["bleName"].is<const char*>()` |
  | server_manager.cpp:487 | `api` | `JsonObject` | `doc["api"].is<JsonObject>()` |
  | server_manager.cpp:489 | `wpasec` | `setWpaSecKey` (string) | `api["wpasec"].is<const char*>()` |
  | server_manager.cpp:492 | `wigle` | `setWigleKey` (string) | `api["wigle"].is<const char*>()` |
  | server_manager.cpp:495 | `pwncrack` | `setPwncrackKey` (string) | `api["pwncrack"].is<const char*>()` |
  | handshake_metadata.h:223 | `wpaSecPassword` | `strncpy` (string) | `doc["wpaSecPassword"].is<const char*>()` |
  | handshake_metadata.h:229 | `gps` | `JsonObject` | `doc["gps"].is<JsonObject>()` |

  `theme` and `karmaChannel` are `uint8_t` settings read from a JSON integer; `is<int>()` is the
  faithful JSON-integer check (ArduinoJson has no `uint8` JSON type). The two `handshake_metadata.h`
  sites are inside a BSSID-sidecar parser that already applies `| default`, so no crash is latent
  there; type-aware is kept for uniformity and to match the string/object reads that follow.

- **ESP-IDF rename.** `esp_ota_get_app_description()` is deprecated in `esp_ota_ops.h` with the
  message "Please use esp_app_get_description instead". `esp_app_get_description()` is declared in
  `esp_app_desc.h` and returns the identical `const esp_app_desc_t *`. Fix: add
  `#include <esp_app_desc.h>` and swap the call. `esp_ota_ops.h` stays included (still used for OTA
  elsewhere in the file).

### What is intentionally not done

No `-Werror` flag, no CI gate, no `platformio.ini` change here either. With this slice our own
`src/` is fully warning-clean, which **unblocks** the gate; the gate itself is the next unit (its
own ADR + CI job, mirroring the cppcheck src/-only scope).

## Verification

- **Primary (the DoD proof):** `rm -rf .pio/build/cardputer/src && pio run -e cardputer`, then
  grep the captured log for `-Wdeprecated-declarations` in our own `src/` (vendored trees
  excluded): 16 before -> 0 after. Record both counts.
- `pio run -e m5stick` -> SUCCESS (the StampS3 twin compiles the same sources).
- `pio test -e native` -> 844/844 (the migrated lines are ESP32-gated / native-excluded; native
  proves the surrounding storage TUs still compile and behave).
- Doc gate: `node scripts/build-index.mjs` + `node scripts/lint-docs.mjs` green with this slice
  added.
- The settings-POST hardening (crash-on-mistyped-field -> reject) is proven by code review of the
  type-aware guards plus the device build; a full on-device POST matrix is the handler's own
  follow-up, not gated here.

## As built

All four Definition-of-Done scenarios were met. 16 physical edit points across 2 files
(2 in `handshake_metadata.h`, 14 in `server_manager.cpp` = 13 `containsKey` + 1 ESP-IDF call +
1 added include), all type-aware per the table above.

- **Warning count (the primary proof).** A forced `src/`-only recompile before the migration
  emitted 16 `-Wdeprecated-declarations` sites in our own `src/`; after, the same recompile
  emits **zero** (measured 2026-10-20, grep over the build log excluding `.pio/`, `.platformio/`
  and the vendored in-`src/` trees). No warning of any other class is introduced: our own `src/`
  is now fully warning-clean, which unblocks the `-Werror` gate. The 130 warnings that remain in
  the full log are all vendored/framework (126 NimBLE `nimconfig.h`, 4 framework
  `USBHIDKeyboard.h`), none in our files.
- **Per site, as applied.** All 15 `containsKey` → `is<T>()` with `T` read-faithful: three
  nested objects → `is<JsonObject>()`; the seven `strncpy`/key-setter string fields →
  `is<const char*>()` (this is the crash fix: a mistyped field is now rejected instead of
  feeding `strncpy` a null `const char*`); `dashboardAuthEnabled` → `is<bool>()`; `theme` and
  `karmaChannel` → `is<int>()`. The ESP-IDF rename swapped `esp_ota_get_app_description()` for
  `esp_app_get_description()` with `#include <esp_app_desc.h>` added; `esp_ota_ops.h` stays (OTA
  is still used elsewhere in the file).
- **Verification (all green).** cardputer `[SUCCESS]`, Flash 74.6% (2,494,651 B, +164 B over the
  post-0081 2,494,487 B: the type-aware `is<T>()` checks emit marginally more code than the bare
  presence check); native `844/844`; m5stick `[SUCCESS]`; doc lint 0 errors. The migrated lines
  are ESP32-gated (`handshake_metadata.h`) or native-excluded (`server_manager.cpp`), so native
  proves the surrounding storage TUs still compile/behave while the two device builds prove the
  migrated code; the unchanged native count confirms no behaviour drift in the shared storage
  files.
- **No deviations from the plan.** The 16-site set, the type mapping, and the ESP-IDF rename all
  landed exactly as designed.
- **Not done, by design.** No `-Werror` flag, no CI gate, no `platformio.ini` change. Our `src/`
  is now fully warning-clean, so the gate (its own ADR + CI job, mirroring cppcheck's src/-only
  scope) is unblocked and is the next unit; the settings-POST hardening's full on-device POST
  matrix is the handler's own follow-up, not gated here.
