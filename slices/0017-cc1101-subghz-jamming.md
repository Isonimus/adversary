---
id: '0017'
title: "CC1101 sub-GHz jamming (carrier + noise, hold-to-jam)"
type: slice
status: accepted
date: 2026-09-08
supersedes: []
superseded_by: []
---

## Goal

Add active sub-GHz **denial** to the CC1101 console. slice-0003 shipped OOK capture and
replay; the same radio, PA, and GDO0 line can hold a channel busy so a fixed-code receiver
(gate/garage/doorbell) never sees its remote. This is the natural third verb on the
sub-GHz radio after capture and replay, and it reuses their entire orchestration —
`cc1101ConfigureOok()` → `cc1101EnterTx()` → drive GDO0 → `cc1101Idle()`.

## Scope

**CC1101 cap, Cardputer only** — the same radio slice-0003 drives. One of the four existing
band presets at a time (315 / 433.92 / 868.35 / 915 MHz); no multi-band sweep (a possible
v2 — it dilutes per-band duty cycle and is more code for less punch per band). Two emission
modes, operator-selectable:

- **Carrier (CW)** — GDO0 held high → a steady unmodulated tone at the preset. Blocks
  narrowband fixed-code receivers.
- **Noise (modulated)** — GDO0 toggled fast (~25 kHz) → energy spread across the channel,
  defeating squelch/preamble detection on receivers that ignore a static carrier.

Out of scope: rolling-code defeat, protocol-aware jamming, reactive/follow jamming.

## Definition of Done

- **Given** the CC1101 cap is resolved and the SD/FSPI bus is owned
- **When** the operator picks Jam → a mode → confirms the transmit warning → holds the jam
  key
- **Then** the CC1101 emits on the selected preset for as long as the key is held, and
  stops the instant it is released

- **Given** an active jam
- **When** the operator presses ESC (or leaves the screen)
- **Then** the radio is returned to idle/power-down and the SD bus is remounted — no stuck
  carrier, no corrupted SD (the pin-conflict invariant)

- **Given** any jam transmission
- **When** it runs
- **Then** it is gated behind an explicit on-screen warning first (jamming is intentional
  interference, illegal to operate on live bands) — no one-keypress accidental transmit

## Design

### Reuse the slice-0003 TX orchestration verbatim

Arming is exactly the replay path minus the RMT frame: `cc1101ConfigureOok(freq)` +
`cc1101EnterTx()` once on entering the active state; `cc1101Idle()` + `SDManager::remount()`
on exit. In TX with GDO0 low the PA emits nothing, so the radio can sit armed and only
radiates while GDO0 is driven — the hold gates emission, not the radio state.

### The jam drive is **bit-banged**, not RMT — on purpose

Replay uses RMT because a fixed-code frame is timing-critical (jitter corrupts the code).
A jammer is the opposite: it wants energy on the channel and does **not** care about edge
precision, so ISR jitter is irrelevant. CLAUDE.md's raw-RF pitfall names this exact case
("bit-banged … jamming monopolizes its pins for the waveform duration; chunk it and poll
G7 between chunks"). So `ookJamBurst(mode, chunkMs)` (in `ook_rmt.cpp`, the GDO0-line unit)
just drives the pin — CW holds it high; Noise toggles it at `JAM_NOISE_HALF_PERIOD_US` —
for one **chunk** (`JAM_CHUNK_MS` = 80 ms), then releases GDO0 to `INPUT_PULLUP`. No RMT
channel is created or destroyed per burst.

### Chunk-and-poll is what makes hold-to-jam safe on the shared matrix

On the original Cardputer, GDO0 (G13) is a keyboard matrix **row**, so a burst that drives
it corrupts a concurrent key scan. The active state therefore emits in 80 ms chunks and,
between chunks (GDO0 released to input), the main loop's keyboard poll runs cleanly. Each
frame, `RadioScreen::update()` reads whether the jam key is still down
(`InputManager::isKeyDownNow`) and emits one chunk if so — release is detected within a
chunk + a frame (< ~150 ms). On the deployment Cardputer ADV the keyboard is on I2C and
does not share the pin, so the poll is conflict-free there; the chunked design is correct on
both.

`InputManager` gains one method, `isKeyDownNow(char)`: a live read of the current key
snapshot (the main loop already polls once per frame) **without** consuming the
`isChange`-gated event path — so ESC still flows through `handleInput()` to stop the jam
while a printable key is polled for the hold. This is the first hold-to-fire UI in the
tree, hence the new input primitive.

### The one host-testable piece: the noise burst plan

Everything else is I/O, but the noise timing has a real quantitative invariant — a units
slip (ms vs µs) would make the burst 1000× too short or long. `planJamNoise(chunkMs,
halfPeriodUs)` is a pure `constexpr` in `ook_rmt.h` returning the toggle-cycle count and a
validity flag (half-period in `(0, RMT/​counter field]`, cycles > 0); it is native-tested
(`test_ook_jam`), mirroring how `cc1101FreqRegs` is tested while the register I/O is not.

### Legal gate

A `JAM_CONFIRM` state mirrors `REPLAY_CONFIRM` but with an interference warning ("illegal to
operate on live bands; authorized tests only") before the radio is armed — the same posture
as the deauth-enable warning (commit d996d77). Jamming can only start via mode-select →
confirm → hold.

### Why no on-device efficacy proof here

Like replay-TX (slice-0003) and per the "don't ship an unverifiable RF feature" bar,
real-world denial can only be shown with a victim receiver, which was not on hand. This
slice is shipped **code-complete**: the pure plan is unit-tested, the arm/teardown reuses
the on-device-proven slice-0003 path, and the chip is confirmed to enter TX. Efficacy
(does a target receiver actually lose its remote) and the hold latency/feel are tracked as
a LEDGER follow-up until a receiver pair is available.

## Verification

- **Native:** `test_ook_jam` covers `planJamNoise` — representative cycle counts, the
  ms/µs units, and rejection of an out-of-range half-period.
- **Build:** `pio run -e cardputer` links the new screen states, HAL primitive, and jam
  drive.
- **On-device (deferred to a LEDGER follow-up):** arm on each preset and confirm the chip
  reaches TX and returns to idle with the SD intact after; with a 433 receiver pair,
  confirm CW denies a fixed-code remote and note whether Noise adds anything on a squelched
  receiver; measure the release latency and tune `JAM_CHUNK_MS` if needed.

## As built

_To be completed once jamming is exercised against a victim receiver on hardware; tracked
in the LEDGER until then._
