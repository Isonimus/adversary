---
id: '0007'
title: "GPS fast presence pre-check at boot"
type: slice
status: accepted
date: 2026-09-06
supersedes: []
superseded_by: []
---

## Goal

Stop the GPS auto-detect from stealing seconds of boot time when **no GPS is attached** —
the common case on this device. Today `GPSManager::init()` runs the full identification
matrix (every pin set × every chip/baud profile) synchronously on the boot path, and when
nothing is on the line it pays **every profile's detection timeout in full** before boot
continues. Measured from the profile table (`gps_config.h`): 3000 + 2000 + 1500 + 1500 =
**8.0 s per pin set** with nothing to find — **~8 s** in the current multi-radio-cap build
(Grove pin set only; the cap-GPS pins are already skipped, slice-0002) and **~16 s** in a
non-cap build (Cap + Grove).

The fix is a **fast carrier pre-check**: before spending the per-profile timeouts on a pin
set, listen briefly for *any* byte. A powered NMEA device puts bytes on the line within one
sentence period; a dead line stays silent. Silence → skip that pin set's whole dwell.
Absent-GPS boot cost collapses from the sum of every timeout to a single short window, while
a GPS that *is* present is still fully identified by the unchanged profile hunt.

## Definition of Done

- **Given** no GPS module attached (Grove empty, cap-GPS pins skipped under the cap)
- **When** the device boots
- **Then** the "Detecting GPS..." boot phase completes in well under a second per probed pin
  set instead of the full sum of profile timeouts, and boot reaches the idle menu measurably
  sooner (target: absent-GPS GPS phase ≤ ~1.5 s, down from ~8 s in the cap build)

- **Given** a working NMEA GPS attached (Grove port) emitting sentences
- **When** the device boots
- **Then** it is still detected, `g_gpsDetected` is set, the detected chip/pin-set is
  reported, and live NMEA continues to parse — no regression in the present-GPS path

- **Given** a GPS attached but momentarily between its 1 Hz sentence bursts when the pre-check
  runs
- **When** the carrier window is evaluated
- **Then** the device is **not** misjudged absent — the presence window spans at least one
  full sentence period so a live-but-bursty GPS is always seen (no false-negative that would
  hand the shared Grove GPIO 1/2 to RFID)

- **Given** the pre-check declares a pin set silent at boot
- **When** the operator later opens a GPS-dependent screen (handshake / wardriving)
- **Then** the existing background re-detection still runs and can pick up a GPS plugged in
  after boot — the boot pre-check only removes the *blocking wait*, not the recovery path

## Design

### Root cause

Detection conflates two jobs: (1) *is a device present?* and (2) *which chip/baud is it?*
Job 2 legitimately needs multi-second per-baud timeouts (a valid `$` may take a moment).
Job 1 does not — a transmitting UART line produces bytes almost immediately at **any** listen
baud (a clean byte at the device's own rate, framing garbage at any other), and a truly empty
line produces none. The current code answers job 1 by paying job 2's full cost against every
profile, which is precisely wasted when the answer is "nothing here".

### The pre-check

Before the profile hunt, per pin set: open the UART at one listen baud, drain any stale bytes,
then watch for the first received byte up to a fixed **presence window**. First byte → carrier
present → run the unchanged per-profile `$` hunt to identify the chip. Window elapses silent →
declare the pin set empty and skip its dwell entirely.

- **One listen baud suffices** because presence is baud-agnostic: bytes (valid or garbage)
  appear at whatever rate we sample a transmitting line. We listen at the cap-GPS default
  (`GPS_PRESENCE_BAUD = 115200`) — the most common attached device — and the subsequent hunt
  still tries every profile baud, so a 9600 Grove GPS is seen as "present" (garbage bytes at
  115200) and then correctly identified at 9600.
- **The window must exceed one sentence period** so a live GPS caught between its ~1 Hz bursts
  is never misjudged absent: `GPS_PRESENCE_WINDOW_MS = 1200` (one 1 Hz period + margin). This
  is a *timeout*, not a fixed dwell — a present device breaks out on its first byte in tens of
  milliseconds, so only the genuinely-absent case pays the full 1.2 s.

Absent-GPS cost: 1 × 1.2 s (cap build) / 2 × 1.2 s (non-cap), down from 8 s / 16 s. Present-GPS
cost: a few tens of ms of pre-check, then the same identification hunt as before.

### Why not the other LEDGER options

- **Shorten `detectionTimeoutMs`** — a band-aid: still linear in the number of profiles, still
  pays every timeout when absent, and cutting the timeout risks missing a slow GPS during the
  `$` hunt (a reliability regression). The pre-check removes the waste at the root instead of
  trimming it.
- **Run detection fully off the boot path** — breaks a load-bearing ordering: RFID shares the
  Grove GPIO 1/2 and its init is skipped **only if** the synchronous GPS result says GPS owns
  those pins (`main.cpp`). Deferring GPS detection would require reworking RFID arbitration and
  introduce a boot-time pin-ownership race — far larger blast radius than the stated bug needs.
  The pre-check keeps arbitration synchronous (GPS resolved before RFID) while making the
  common answer cheap. The absent-GPS *recovery* path (background re-detect from the GPS
  screens) already exists and is untouched.

### What is native-testable (the pure core), and what is on-device only

The slice-0003/0006 discipline: prove the decision logic on the host, leave only the true UART
boundary for the device.

- **`gps::gpsProbePhase(anyByteSeen, elapsedMs, windowMs) → ProbePhase`** — the pure presence
  policy (`Present` the instant a byte is seen; `Absent` only once the window has fully
  elapsed silent; `Waiting` otherwise). A header-only `constexpr` free function
  (`src/modules/gps/gps_probe.h`), unit-tested for the two properties the whole fix rests on:
  a single byte flips to `Present` immediately (the speed win), and `Absent` is **never**
  declared before the window elapses (the no-false-negative guard). Fail-loud by construction.
- **On-device only** (HAL/UART boundary, CLAUDE.md: HAL is mock/manual): the actual carrier
  detection against real silence vs. a real GPS, the measured boot-time reduction, and that a
  present GPS still identifies and parses. Recorded in `## As built`.

### Module layout

- `src/modules/gps/gps_probe.h` — the pure `ProbePhase` + `gpsProbePhase()` policy. No
  hardware, no cpp; included by both `gps_manager.cpp` and the native test.
- `src/modules/gps/gps_config.h` — add `GPS_PRESENCE_BAUD` and `GPS_PRESENCE_WINDOW_MS` beside
  the existing UART constants (named, not bare literals).
- `src/modules/gps/gps_manager.{h,cpp}` — `init()` gains a `probePinSetCarrier(pins)` step
  (firmware-only) that runs the pre-check via `gpsProbePhase()`; the per-profile `$` hunt runs
  only when the carrier is seen. No change to `update()`, the background task, or the screens.

## Verification

- **Native unit test** (`pio test -e native`), fail-before (function absent → compile fail) /
  pass-after:
  - `test/test_gps_probe/test_gps_probe.cpp` — `gpsProbePhase()`: a byte at t=0 → `Present`;
    a byte partway through the window → `Present` (early break, not made to wait); silence
    below the window → `Waiting` (must keep listening); silence at/after the window → `Absent`
    (the boundary — off-by-one here would either false-negative a live GPS or never terminate).
- **On-device operator checklist** (the UART boundary that cannot run natively), recorded in
  `## As built`:
  - Boot with **no GPS attached** and time the "Detecting GPS..." phase / total boot vs. the
    prior ~8 s — this is the bug's scenario and the primary acceptance measure.
  - Boot with a working Grove GPS attached → still detected, chip/pin-set reported, live NMEA
    parses (present-path no-regression).
  - Plug a GPS in *after* an absent boot, open the handshake or wardriving screen → background
    re-detection still finds it (recovery path intact).

## As built

Shipped as designed: the pure `gps::gpsProbePhase()` presence policy + `ProbePhase` enum in
`src/modules/gps/gps_probe.h` (header-only, hardware-free), the `GPS_PRESENCE_BAUD` (115200)
and `GPS_PRESENCE_WINDOW_MS` (1200) constants beside the existing UART settings in
`gps_config.h`, and a firmware-only `GPSManager::probePinSetCarrier()` that `init()` now runs
before the per-profile hunt — the hunt is skipped on any pin set the pre-check finds silent.
`update()`, the background re-detection task, and the screens are untouched.

**Verified on-device (Cardputer ADV, 2026-09-06)**, both DoD hardware scenarios, measured over
serial with host-side timestamps:

- **Absent GPS** (multi-radio cap seated → cap-GPS pins skipped, Grove empty): the "Detecting
  GPS" phase took **1.204 s** — a single Grove presence window — down from the ~8.0 s sum of
  every profile timeout this cap build used to pay. The log shows `Grove: no carrier, skipping`
  (the hunt skipped) and `will retry on GPS-dependent screens` (the untouched recovery path
  still armed).
- **Present GPS** (a GNSS cap; CC1101/NRF24 absent so the cap resolved `None` and the Cap GPS
  pin set was probed): detected as `AT6668 on Cap` at 115200 in **0.702 s** — the pre-check
  broke out on the first byte and the hunt's first profile locked immediately — followed by a
  live GGA fix (8 sats, hdop 2.3, valid coordinates) and a successful GPS time sync. No
  present-path regression.
- **RFID arbitration** ran synchronously after the GPS verdict in both boots (skipped when GPS
  claimed GPIO 1/2, probed when GPS was absent) — the load-bearing Grove ownership order the
  "run detection off the boot path" option would have broken is intact.
- The pre-check is thereby exercised on **both** pin sets: Grove (absent → fast skip) and Cap
  (present → fast detect).

Native + compile green: `test_gps_probe` 6/6, full suite **698/698**, cardputer build SUCCESS
(RAM 26.5%, Flash 72.4%). The after-boot *recovery* path (a GPS plugged in after an absent
boot, picked up when a GPS screen opens) is pre-existing, untouched code and was confirmed
armed by the absent-boot log, not re-run end-to-end — no change to that mechanism in this
slice.
