---
id: '0004'
title: "Cap IR record & replay + IR TX-source capability"
type: slice
status: accepted
date: 2026-09-06
supersedes: []
superseded_by: []
---

## Goal

Turn the multi-radio cap's IR receiver into a **learn-and-replay** feature — the IR analog
of slice-0003's sub-GHz console. The operator points a household remote at the cap, captures
the frame, names and saves it to SD, and replays it at any time. Captured frames are
**decoded when the protocol is recognised** (NEC / RC5 / Sony / … via `IRremoteESP8266`,
already a dependency) and stored compactly and human-readably; **unknown frames fall back to
raw timings** so nothing is unreplayable. This closes the two IR items the cap-detection work
left open: LEDGER `[deferred] IR TX source …` and `[feature] IR record & replay …`.

The slice also introduces the small **IR TX-source capability** those items call for. IR
*receive* exists **only** on the cap (`CAP_IR_RX`, the platform has no built-in receiver), so
capture hard-requires the cap. IR *transmit* has two paths: the **built-in emitter** (`IR_TX`
= 44, already proven by TV-B-Gone) and the cap's **3-emitter array** (`CAP_IR_TX` = 6, more
range/angle, never yet exercised). Replay defaults to the proven built-in emitter with a
Settings override to the cap array.

Out of scope: rolling-code/pairing protocols (verbatim replay cannot defeat them) and an
AC-specific state-machine encoder.

## Definition of Done

- **Given** a multi-radio cap seated and a household remote (TV/soundbar/etc.)
- **When** the operator opens `IR → Learn` and presses a button on the remote
- **Then** the frame is captured, shown decoded (protocol + value) when recognised or as a raw
  pulse train otherwise, and the operator can name and save it to `/adversary/ir/<name>.ir`,
  where it appears in the saved-codes list

- **Given** a previously saved IR code in the list
- **When** the operator selects it and confirms Replay
- **Then** it is re-emitted on the configured TX source — a decoded code via its typed
  protocol send, a raw code via `sendRaw` at its stored carrier — and the target appliance
  responds

- **Given** no multi-radio cap is resolved
- **When** the operator reaches `IR → Learn`
- **Then** capture is shown unavailable with a clear reason ("no IR receiver without the cap"),
  nothing crashes, and TV-B-Gone plus replay of already-saved codes over the built-in emitter
  still work

- **Given** the IR TX-source setting is *built-in* vs *cap array*
- **When** a replay runs
- **Then** the selected emitter transmits (both actuate the target), and the choice persists in
  `WirelessSettings`

- **Given** a decoded code and a raw-fallback code
- **When** each is saved and reloaded from SD
- **Then** both round-trip losslessly, and a truncated/oversized/garbage `.ir` file is rejected
  loudly (no silent default) rather than replayed as noise

## Design

### Decode + raw fallback — why this diverges from slice-0003's raw-only

slice-0003 stored sub-GHz OOK as **raw pulses only**, correctly: no OOK decoder was available.
IR is the opposite case — decoding is `IRremoteESP8266`'s entire purpose and is already linked.
Decoded storage wins on three axes that matter here:

- **Replay reliability.** Raw IR replay is timing-fragile across capture/replay hardware; a
  typed `sendNEC(value, bits)` reconstructs clean carrier-modulated timing and is far more
  likely to actuate the target than re-emitting captured edges.
- **Human-readable library.** `Samsung TV Power (NEC 0xE0E040BF)` is a usable saved-code name;
  a 200-edge blob is not.
- **Size.** A decoded code is a handful of bytes vs a raw edge list.

Raw is kept as the **fallback** (unknown protocol, or `decode()` returns nothing) so coverage
stays universal — the best of both, justified by the asymmetry with OOK, not by fashion.

### IR is *not* on the SPI/SD bus — simpler than OOK

Unlike the CC1101 path, IR RX/TX are plain GPIO driven by `IRremoteESP8266` (RMT under the
hood). There is **no shared-bus borrow-and-remount** and no SD-corruption risk from an IR
transaction — the slice-0002 bus dance does not apply. Two caveats to honour instead:

- **RMT coexistence.** Both `IRrecv` (this slice) and `RadioScreen`'s `ook_rmt` (slice-0003)
  use the ESP32 RMT peripheral. They are never active at once (different screens), but each
  must **release its RMT channel on screen exit** so the other can claim it. Capture/replay
  own the channel only for the operation, then free it.
- **Matrix-row pins.** `CAP_IR_RX` (5) and `CAP_IR_TX` (6) are keyboard-matrix rows on the
  *original* Cardputer; on the deployment **Cardputer ADV** (I2C keyboard) the conflict is
  dormant (CLAUDE.md). Bring-up must confirm no interference on the ADV — the one genuinely
  unproven hardware path (see Verification).

### The IR TX-source capability — small, local, not `HardwareManager`

Model IR as a tiny capability, resolved next to the pins it describes — **not** folded into the
not-yet-existing `HardwareManager` (same speculative-generality reasoning as slices 0002/0003;
it migrates there if that slice ever happens):

- `hasIrRx()` → `resolvedExpansionCap() == MultiRadio` (deduced from cap presence; an IR
  receiver has no feasible independent probe — idle-high is indistinguishable from a floating
  pull-up).
- `hasIrTx()` → always `true` (built-in emitter).
- `irTxPin(setting)` → `setting == CapArray ? CAP_IR_TX : IR_TX`, the override persisted in
  `WirelessSettings`. TV-B-Gone can adopt the same resolver later (it currently hardcodes 44).

### Module layout

Mirrors slice-0003's split (pure codec + firmware HAL-ish capture/replay + screen), and reuses
the **capture → name → save → list → act** pattern now written down in `docs/UI-STYLE.md`
(this is its third consumer — the rule-of-three that motivated the guide):

- `src/modules/ir/ir_signal.{h,cpp}` — the in-memory IR code and its `.ir` (de)serialisation.
  A tagged union: **decoded** `{protocol, value, bits, address?, command?}` or **raw**
  `{carrierHz, timings[]}`. Line-based text `.ir` format (Flipper-compatible-ish, human-
  readable). Pure; native-testable, fail-loud on malformed input — the direct analog of
  `ook_signal`.
- `src/modules/ir/ir_capture.{h,cpp}` — `IRrecv` bound to `CAP_IR_RX`: `capture(window) →
  ir_signal`, running `decode()` and falling back to the raw buffer. Firmware-only.
- `src/modules/ir/ir_replay.{h,cpp}` — `IRsend` on `irTxPin(setting)`: typed send for decoded
  codes, `sendRaw` for raw. Firmware-only. (The TV-B-Gone `emitIrBurst` helper stays TV-B-Gone-
  specific; general replay uses the stored carrier, no Sony special-case.)
- `src/modules/ir/ir_capability.h` — the `hasIrRx` / `hasIrTx` / `irTxPin` free functions above.
- `src/ui/screens/ir_record_screen.{h,cpp}` — a new screen mirroring `RadioScreen`: **Learn**
  (capture → name → save) and **Saved codes** (list → Replay/Delete) as state-enum sub-states.
  Built **to `docs/UI-STYLE.md`** from the start (per-state footer hints + dispatch,
  `BG_SELECTED` highlight, standard keys, state-enum sub-nav), so it does not repeat
  RadioScreen's divergences.

**Screen integration — the Infrared submenu already exists.** The `Infrared` carousel entry
opens the `irMenu` submenu (`main.cpp`), today holding only `TV-B-Gone`. The submenu *is* the
hub, so this slice simply adds **sibling entries beside TV-B-Gone**: a `Record / Replay` action
→ the new `IrRecordScreen`, and `TV-B-Gone` (→ `IrTvBGoneScreen`) is **left untouched** — no
hub-screen refactor and no re-implementation. The IR **TX source** is a setting persisted in
`WirelessSettings`, surfaced in the submenu for quick access (Built-in ↔ Cap array). Engines
stay separate modules the screen orchestrates: Learn drives `ir_capture`, Replay drives
`ir_replay`. (Splitting Learn and Saved into two separate submenu entries, rather than one
`Record / Replay` screen with two sub-states, is a trivial variation if preferred.)

### What is deliberately NOT built

- **No `HardwareManager`** (as above).
- **No rolling-code / pairing handling** — verbatim/typed replay cannot defeat it.
- **No AC state-machine protocols** — `IRremoteESP8266`'s A/C encoders are a large surface;
  raw fallback still captures a single AC frame, which suffices for fixed commands.

## Verification

- **Native unit tests** (`pio test -e native`), each fail-before/pass-after:
  - `test/test_ir_signal.cpp` — `.ir` (de)serialisation round-trips both a decoded code
    (protocol/value/bits) and a raw code (carrier + timings); rejects an empty, oversized, and
    malformed file (fail-loud, no silent default). The analog of `test_ook_signal`.
  - `test/test_ir_capability.cpp` — `hasIrRx()` tracks the resolved cap; `irTxPin()` returns
    `IR_TX` vs `CAP_IR_TX` per the setting. Pure, no hardware.
- **On-device operator checklist** (the RX/TX hardware boundary — HAL is manual per CLAUDE.md),
  recorded in `## As built`. Unlike slice-0003's replay, this is **fully exercisable now**: IR
  sources (any remote) are ubiquitous, so capture *and* replay round-trip get verified this
  slice, not deferred.
  - **Cap IR RX bring-up** — the unproven path: does `CAP_IR_RX` receive at all, and cleanly, on
    the ADV with no keyboard-matrix interference? Capture a known remote and confirm a stable
    decode across repeated presses.
  - Capture a recognised remote (e.g. an NEC TV) → protocol + value shown → save → replay over
    the **built-in** emitter → the TV responds.
  - Capture an unrecognised remote (raw fallback) → save → replay → the appliance responds.
  - Flip the TX-source setting to the **cap array** → replay again → the target still responds
    (confirms `CAP_IR_TX` drives the emitters), and the setting persists across a reboot.
  - Cap absent → `IR → Learn` unavailable with the reason shown; TV-B-Gone and saved-code replay
    over the built-in emitter still work.
  - Open `IR Learn` then `Radio` (or vice-versa) in one session → both capture paths work,
    confirming the RMT channel is released on screen exit.

## As built

_(Filled at merge, once the on-device checklist above has been run — capture/decode/replay
numbers, the RMT-release approach that shipped, the `.ir` format finalised, any deviation, and
the confirmed TX-source behaviour on both emitters.)_

_Filled below (2026-09-06); the placeholder above is kept verbatim because the doc body is
append-only (enforced by `scripts/check-immutable.mjs`). Implementation landed on
`feature/hid-menu` and the on-device checklist is partly run — what is verified vs. still
pending for merge is called out at the end._

**Shipped (native-verified):**

- `ir_signal.{h,cpp}` — the tagged-union `IrSignal` (Parsed `{protocol, value, bits}` / Raw
  `{carrierHz, timingsUs}`) and a line-based text `.ir` codec (magic `IR1`). Kept **Parsed**
  minimal — `{protocol, value, bits}` is exactly what IRremoteESP8266's generic
  `IRsend::send()` consumes, so the design's optional `address?/command?` were dropped (KISS;
  they add nothing the replay path uses). `test_ir_signal` (17 cases): both encodings
  round-trip (incl. a wide 64-bit value), and serialize/deserialize fail loud on every
  malformed input (bad magic, missing/unknown type, missing mandatory field, malformed number,
  junk line, oversized timing list).
- `ir_capability.h` — `capHasIrRx(cap)` (pure), `irTxPin(source)` (pure), and firmware
  `hasIrRx()`/`hasIrTx()`. `test_ir_capability` (2 cases) pins the RX-tracks-cap and
  emitter-selection behaviour. `IrTxSource` persists in `WirelessSettings` (`irTxSource`,
  load/save wired).
- `ir_capture.{h,cpp}` (Cardputer-only) — `IRrecv` on `CAP_IR_RX`, decoding when recognised
  and falling back to a raw train at `IR_DEFAULT_CARRIER_HZ` (38 kHz — the receiver
  demodulates the carrier away, so it can't be measured). Native/M5Stick get a stub.
- `ir_replay.{h,cpp}` (ESP32) — typed `send()` for Parsed, `sendRaw()` for Raw, with an
  in-band carrier bounds check (30–60 kHz) so a corrupt file fails loud rather than truncating
  into IRsend's 16-bit carrier arg. Native stub.
- `IrRecordScreen` — built to `docs/UI-STYLE` (per-state footer hints + dispatch,
  `BG_SELECTED` highlight, `LIST_ITEM_HEIGHT` rows, standard keys, state-enum sub-nav). Learn /
  Saved codes / TX-source sub-states; **partial availability** — only Learn gates on the cap,
  replay of saved codes over the built-in emitter works with no cap. Sibling entry
  `Record / Replay` added to the existing Infrared submenu; `IrTvBGoneScreen` untouched.

**RMT coexistence — how it shipped:** rather than "release on screen exit", capture and replay
each construct their `IRrecv`/`IRsend` locally and release it before returning (`disableIRIn()`
/ scope exit), so the peripheral is never held across screens — a strictly stronger guarantee
than the design asked for, and no persistent handle to leak.

**Build:** `pio test -e native` 674/675 (only the pre-existing `test_time_manager` failure,
tracked in LEDGER); `pio run -e cardputer` SUCCESS, Flash 70.0% → 72.2% (the IRremoteESP8266
decode/encode tables), RAM 26.5%.

**Bring-up finding — double-teardown crash (fixed).** First on-device capture panicked with a
`LoadProhibited` (`EXCVADDR 0x1c`) in `~IRrecv()`. Root cause: `captureIrSignal` called
`receiver.disableIRIn()` explicitly *and* the `IRrecv` destructor calls it again at scope exit;
on ESP32-arduino-core v3 `disableIRIn()` runs `timerEnd()`, which frees the hardware timer but
leaves the library's `timer` pointer dangling, so the second teardown's `timerWrite()` hit freed
memory. Fixed by removing the explicit call and relying on RAII (one teardown, at scope exit) —
`ir_capture.cpp` carries a comment so it is not reintroduced. Not natively testable: the whole
unit is `TARGET_CARDPUTER`-guarded and the defect is in the real timer teardown, which a mock
would not reproduce — this is HAL/manual-verification territory.

**On-device verified (2026-09-06, direct-flash):** capture → decode → save → **built-in**
emitter replay actuated the target; **cap-array** replay (TX source flipped) also actuated it;
capture/decode/replay round-trip works. (Implicitly confirms cap IR-RX bring-up on the ADV — no
keyboard-matrix interference — and the RMT/timer release, since a second capture in the same
session succeeds.)

**Remaining for merge:** TX-source persistence across a reboot, the cap-absent "Learn
unavailable / replay still works" path, and the explicit RMT-release cross-check with the Radio
screen (IR Learn then Radio in one session). Verified there, then the LEDGER items close.
