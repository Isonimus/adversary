---
id: '0020'
title: "Graceful boot with no SD card (remove the hanging default-SPI fallback)"
type: slice
status: accepted
date: 2026-09-11
supersedes: []
superseded_by: []
---

## Goal

Booting the firmware with **no SD card inserted** must reach the idle menu like any other
boot, with SD-backed features cleanly unavailable and a recovery path that needs no reboot.
Today a cardless boot **freezes**: the device hangs forever in `SDManager::init()` and never
reaches the UI. This is the common case for a quick RF/BLE session where the operator hasn't
seated a card, and a hang with no on-screen explanation reads as a bricked device.

## Definition of Done

- **Given** no SD card inserted
- **When** the device boots
- **Then** it reaches the idle menu every time — `SDManager::init()` returns within a bounded
  time with status `NO_CARD`, and the boot never hangs at the "Mounting SD card" stage

- **Given** a cardless boot has completed
- **When** the idle menu is shown
- **Then** the operator is told once, clearly, that storage features are unavailable (a toast),
  rather than left to infer it from silent failures

- **Given** the operator inserts a card after a cardless boot
- **When** they trigger the "Retry SD mount" action (Settings)
- **Then** the card mounts without a reboot, the `NO_CARD` verdict is cleared, and storage
  features become available — the existing `remount()` recovery path, surfaced to the UI

- **Given** an SD card **is** inserted
- **When** the device boots
- **Then** it mounts exactly as before — no regression to the working dedicated-FSPI path, the
  card type/size are logged, and the directory structure is created

## Design

### Root cause (measured, not inferred)

A no-card boot was captured over serial (host-timestamped, `scratchpad/serial_boot_trace.py`,
2026-09-11). `SDManager::init()` runs three mount strategies in sequence:

- **Method 1** (launcher pre-mount): `SD.cardType()` reads `CARD_NONE`, skipped instantly.
- **Method 2** (dedicated FSPI bus, the `sdSPI(FSPI)` instance): the 5-frequency ladder
  (4/10/20/1/0.4 MHz) runs **bounded and clean** — every frequency fails its `GO_IDLE_STATE`
  and returns in ~1.1 s, ~5.6 s total, exactly as a cardless bus should.
- **Method 3** (default `SPI` bus fallback): calls `SPI.begin(sclk, miso, mosi, cs)` on the
  **same pins the FSPI `sdSPI` instance already owns**. The log emits
  `addApbChangeCallback(): duplicate func` — the SD driver's APB-frequency callback is already
  registered from Method 2 — and then `SD.begin(sd_cs, SPI, 400000)` **spins forever** in the
  Arduino SD driver's `sdWait()`. The trace stops dead at `Default SPI at 400000 Hz...` with no
  further output for 46 s+ (no heartbeats, no next frequency, no "all methods failed"). **This
  is the freeze.**

The device never reaches the existing non-fatal `if (!sdManager.init()) { /* Continue anyway */ }`
handler in `setup()`, nor `redetectModules()` — it dies inside the *first* `init()`, in Method 3.

Provenance: Method 3 has been present since the **initial commit**, born alongside Method 2 as a
speculative "try every bus" fallback — never a deliberate fix for a card Method 2 missed. On this
board Method 2 (dedicated FSPI) is the proven and only working mount path; Method 3 re-initialises
a *second* SPI peripheral on pins FSPI already drives, which is precisely why it collides and
hangs. It is the speculative generality CLAUDE.md §3 forbids, and it is load-bearing for nothing.

### The fix

Three layers, smallest-blast-radius first:

1. **Remove Method 3 entirely** (the freeze fix). After the bounded Method 2 ladder fails with no
   card, `init()` sets `m_status = NO_CARD` and returns `false` cleanly. This alone makes the
   cardless boot terminate and reach idle — a root-cause removal, not a guard bolted over a hang.

2. **Cache the `NO_CARD` verdict for the boot** (bounded cost). With no card-detect pin, the only
   way to answer "is a card present?" is to attempt the real SPI mount, so the ~5.6 s Method 2
   ladder is the presence test — but it must run **once**, not repeatedly. `redetectModules()`
   calls `remount()` up to twice more on the boot path (claim-bus + post-cap-probe re-sync); once
   a `NO_CARD` verdict is set, `remount()` short-circuits to a no-op and returns the cached verdict
   **unless** called with an explicit `forceRetry` (the operator's retry). This keeps a cardless
   boot to a single mount attempt instead of three.

3. **Degraded-mode feedback + recovery** (the operator-facing half). On a `NO_CARD` boot, raise
   one toast at idle ("No SD card — captures, logs and dashboard disabled"). SD-dependent managers
   already guard on `isReady()`/`m_initialized`, so nothing crashes; the gap is that failures are
   silent. Add a **Settings → "Retry SD mount"** action that calls `remount(forceRetry=true)`,
   clearing the verdict and mounting a card inserted after boot — the CLAUDE.md "allow retry
   without reboot" contract. Per-feature greying of every menu entry is deliberately **out of
   scope** for v1 (a broad UI change); a triggered SD action without a card shows a targeted toast
   instead.

4. **Reconcile settings on retry-mount** (a data-loss guard the retry *created*). A cardless boot
   skips `SettingsManager::load()`, so in-memory settings are compiled **defaults** (Red Team theme,
   `themePreset = 0`). Mounting the card via retry does not, by itself, reload them — so the Settings
   screen's edit buffer and the exit autosave still hold defaults, and exiting Settings writes those
   defaults **over the card's real values** (a saved Cyberpunk theme was clobbered back to default —
   found on-device 2026-09-11). The fix: on a successful `remount(forceRetry=true)`, reload the whole
   settings struct from the card, re-apply every runtime-applied setting (theme, brightness,
   notification audio/LED, toast position), reload the whitelist + API-key state, and reseed the
   Settings screen's `temp*_` edit buffer — so the card becomes the in-memory truth before any write.
   Card-as-source-of-truth is the chosen priority: a setting *changed while cardless* and not yet
   saved is discarded in favour of the card's persisted value, because never destroying already-saved
   data outranks preserving an edit made when there was nowhere to save it. The rejected alternative —
   suppressing the exit autosave — is more fragile and leaves the in-memory/card mismatch for the next
   writer to trip over.

### Why not the alternatives

- **Guard Method 3 with a timeout / watchdog instead of removing it** — a workaround that keeps
  dead, conflicting code and masks the real defect (CLAUDE.md §3: no hacks, fix the root). Method 3
  cannot succeed where the dedicated-FSPI ladder on the same pins failed; there is nothing to
  rescue by keeping it.
- **A fast SD presence pre-check (the slice-0007 GPS analogue)** — GPS had a cheap, bus-agnostic
  presence signal (any UART byte). SD has none: distinguishing "no card" from "card needs a lower
  clock" is the entire reason the frequency ladder exists, so the honest presence test *is* the
  bounded ladder. Shrinking the absent-case ladder (e.g. a single tolerant 400 kHz `CMD0` probe) is
  a possible future optimisation, not needed to fix the freeze, and is left out to avoid premature
  complexity. The verdict cache (layer 2) removes the *repetition*, which is the part that actually
  compounds the cost.
- **Hard-fail / error screen on no card** — wrong for a red-team tool whose RF/BLE features need no
  card. Degrade, don't block.

### What is native-testable, and what is on-device only

Per the slice-0003/0006/0007 discipline — prove the decision logic on the host, leave only the true
hardware boundary for the device:

- **Native-testable (pure):** the verdict/short-circuit policy — given a prior `NO_CARD` verdict
  and `forceRetry = false`, `remount()` must not re-attempt and must return the cached verdict;
  with `forceRetry = true` it must clear the verdict and re-attempt. This is the logic that keeps a
  cardless boot bounded, and an off-by-one here either reintroduces the triple-mount cost or blocks
  a legitimate retry. Exercised against the `UNIT_TEST` mock `SDManager`.
- **On-device only** (HAL/SPI boundary, CLAUDE.md: HAL is mock/manual), recorded in `## As built`:
  the cardless boot now reaches idle and its bounded `init()` duration; the present-card boot still
  mounts (no regression); the Settings retry mounts a card inserted after boot; and — because the
  multi-radio cap shares the FSPI bus — a **cap-attached, no-card** boot still reaches idle and is
  no worse than today for cap detection. (Cap detection is *already* gated behind a successful card
  mount on `main`: `probeCC1101/NRF24` return `false` when `spiBus()` is null, and `spiBus()` is
  non-null only after a card mounts, so the cap is undetectable cardless today — independent of this
  slice. That pre-existing gap is logged separately; the layer-2 verdict no-op cannot make it worse,
  since the pre-change remount could not hand the probe a bus without a card either.)

### Module layout

- `src/hal/storage/sd_manager.cpp` — delete the Method 3 block; set `NO_CARD` and return after the
  Method 2 ladder; add the boot-scoped verdict cache and a `forceRetry` parameter to `remount()`.
- `src/hal/storage/sd_manager.h` — `remount(bool forceRetry = false)` signature; any verdict query
  the UI needs.
- `src/main.cpp` — raise the no-card toast at idle; `redetectModules()` remounts become no-ops
  under the verdict (no code change if `remount()` self-guards).
- Settings screen — a "Retry SD mount" action calling `remount(true)`.
- `test/test_sd_verdict/…` — the pure verdict/short-circuit unit test.

## Verification

- **Native** (`pio test -e native`), fail-before/pass-after: the verdict/short-circuit test —
  a cached `NO_CARD` + `forceRetry=false` does not re-attempt (returns the verdict); `forceRetry=true`
  clears it and re-attempts. Plus the full suite green.
- **Build**: `pio run -e cardputer` SUCCESS (record RAM/Flash).
- **On-device operator checklist** (the SPI boundary, recorded in `## As built`):
  - Boot with **no card** → reaches idle (the bug's scenario); capture the serial trace and the
    bounded `init()` duration (contrast the prior infinite hang at `Default SPI at 400000 Hz...`).
  - Boot with a **card inserted** → mounts, type/size logged, directories created (no regression).
  - **Insert a card after a cardless boot → Settings → Retry SD mount** → mounts without reboot,
    storage features become available.
  - **Settings-clobber regression** (the data-loss guard): save a non-default theme (e.g. Cyberpunk)
    to the card, boot **without** the card (idle shows the default Red Team theme), insert the card,
    Retry SD mount, then **exit Settings** → the saved theme is restored and still on the card, not
    overwritten by the default. This is the exact scenario that failed before the reconcile fix.
  - Boot **cap attached, no card** → cap still detected and idle still reached (shared-FSPI-bus
    no-starvation check).

## As built

Shipped as designed. `SDManager::init()` drops the Method 3 default-`SPI` fallback and returns a
clean `NO_CARD` after the dedicated-FSPI ladder; a boot-scoped `m_noCardVerdict` plus the pure
`sdShouldAttemptRemount()` policy (`src/hal/storage/sd_mount_policy.h`) make `remount()` a no-op once
a cardless boot is known, with `remount(forceRetry=true)` clearing it for the operator retry. A
no-card boot raises a WARNING toast at idle; **Settings → Storage → Retry SD Mount** calls
`remount(true)` and, on success, reconciles the whole settings struct with the card
(`load()` + `loadWhitelist()` + `checkApiKeyFile()` + re-apply theme/brightness/notifications/toast
position + `buildSettingsList()`) before the exit autosave can run.

**Root cause was measured, not inferred** — the freeze is a device-only SPI-driver hang that the
native test cannot reach, so the on-device trace is what pinned it (as with the earlier screenshot
colour bug). The pure `sdShouldAttemptRemount()` policy is the part that *is* host-testable and is
covered by `test_sd_mount_policy`.

**Verified on-device (Cardputer, 2026-09-11)**, captured over serial with host timestamps
(`scratchpad/serial_boot_trace.py`):

- **No-card boot** — the FSPI ladder ran bounded (4→10→20→1→0.4 MHz, ~5.6 s) then
  `[SDManager] No card mounted on the dedicated FSPI bus.` → `SD Card Error: NO_CARD`, and boot
  reached the idle menu (`[Main] showMenu()` + heartbeat, heap ~201 KB). **No hang** — contrast the
  prior firmware, which sat silent for 46 s+ at `Default SPI at 400000 Hz...`. The two
  `redetectModules()` re-mounts logged `remount skipped: no card (cached verdict)` — instant no-ops,
  confirming the verdict cache prevents the triple ladder. The "No SD card…" toast appeared at idle
  (operator-confirmed).
- **Card-present boot (no regression)** — mounted on the first attempt (`Trying 4MHz... SUCCESS`,
  `Card Type: SDHC`, 15 GB), and the post-cap-probe re-mount logged `SD re-mount after cap probe: OK`.
- **Retry without reboot** — with the device idle and cardless, inserting the card and running Retry
  SD Mount logged `Initializing SD card... Trying 4MHz... SUCCESS` then `[Settings] Loading
  configuration...` + `Loading whitelist... Loaded 3 entries` — the reconcile firing.
- **Settings-clobber regression fixed** — repro: Cyberpunk saved to card → cardless boot showed the
  Red Team default → insert card → Retry SD Mount snapped the theme back to Cyberpunk (operator-
  confirmed) → exiting Settings logged `Saving configuration...` (now writing the reloaded values) →
  a final reset booted **Cyberpunk** (operator-confirmed). Before the reconcile fix this exact flow
  overwrote the saved theme with the in-memory default.

Native + build green: `test_sd_mount_policy` 3/3, full suite **749/749**, cardputer build SUCCESS
(RAM 26.4%, Flash 73.8%). The cap-attached-no-card case is bounded by the pre-existing "cap
undetectable without SD" gap (logged separately in LEDGER) and was not made worse — `spiBus()` still
returns null without a mounted card, exactly as before.
