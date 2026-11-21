---
id: '0165'
title: "Autohunt UI rolling status line"
type: slice
status: accepted
date: 2026-11-21
supersedes: []
superseded_by: []
---

## Goal

Surface the autohunt engine's inner moves to the operator. Today the autohunt screen
shows the coarse phase label (SCANNING / LOCKING / ATTACKING / WAITING), a progress
bar, the "Xs remaining" countdown, and the EAPOL M1/M2/M3/M4/PMKID strip. Everything
else (QoS-Null stimulation, deauth bursts, PMKID solicitation, BTM steer frames,
discovered clients) is invisible unless the operator is tethered to a serial console.

The slice-0162 field pass made the gap concrete: the engine fired a QoS-Null that
woke a client, which showed up on serial, but nothing on the screen said so. The
operator sees a quiet progress bar and cannot distinguish "nothing provoked" from
"we provoked but you cannot see it."

ADR-0164 locked the design: a direct `std::function` sink on `HandshakeCapture`
that `HandshakeScreen` sets at `onEnter` and clears at `onExit`, with a
priority-with-dwell overlap policy, inline rendering after the "Xs remaining"
countdown (space economy, user-chosen 2026-11-21), and serial parity audited at
each callsite rather than wrapped around the sink.

Folds in the LEDGER item **"Autohunt UI rolling status line (pairs with the
scoring work)"** (ADR-0164 is its design record; this slice is the delivery).

## Definition of Done

### Scenario 1: engine emits visible status strings during an autohunt attack window

- **Given** autohunt is running on a target and has entered the ATTACKING phase
- **And** the attack window observes a client via the promiscuous listener (slice-0163)
- **When** `HandshakeCapture` surfaces that client on channel
- **Then** the autohunt screen renders `Client XX:XX:XX:XX:XX:XX` on the same line as
  `"Xs remaining"`, separated by `" · "`, in `theme::TEXT_SECONDARY()`
- **And** the status half dwells for 2000 ms before another priority-2 message can
  replace it

### Scenario 2: priority preemption on handshake capture

- **Given** the status line is currently rendering a priority-2 message (e.g.
  `QoS-Null to 2 clients`) with 1500 ms of dwell remaining
- **When** the four-way handshake completes and the sink receives
  `Handshake captured` at priority 4
- **Then** the status half is replaced immediately, before the pending dwell expires
- **And** `Handshake captured` dwells for 4000 ms (priority-4 dwell), not 2000 ms

### Scenario 3: no serial log duplication at audited callsites

- **Given** a callsite that already writes a serial log line today (e.g. the QoS-Null
  fire path in `handshake_capture.cpp`)
- **When** the sink is invoked at that callsite
- **Then** no new `LOG_I` / `LOG_W` / `LOG_D` call is introduced next to it; the sink
  is wired alongside the existing log, not around it
- **And** callsites that lack a matching serial line gain one in the same change, so
  every rendered string has an equivalent serial trace

### Scenario 4: screen cleanup on exit

- **Given** the autohunt screen is active with the sink set on `HandshakeCapture`
- **When** the screen exits (operator presses ESC, or autohunt completes)
- **Then** `HandshakeCapture::clearStatusSink()` is called in `onExit()`
- **And** the sink pointer in `HandshakeCapture` is cleared so a subsequent emit from
  a background capture does not write into a destroyed screen buffer

## Design

### Sink API on `HandshakeCapture`

Add to `src/modules/capture/handshake_capture.h`:

```cpp
using StatusSink = std::function<void(const char* msg, uint8_t priority)>;

void setStatusSink(StatusSink sink) { statusSink_ = std::move(sink); }
void clearStatusSink() { statusSink_ = nullptr; }

private:
    StatusSink statusSink_;

    // Internal helper, inlined at all emit callsites via:
    //   if (statusSink_) statusSink_(msg, priority);
    // Not a macro; a method taking (const char*, uint8_t). See emit callsite list below.
    void emitStatus(const char* msg, uint8_t priority) {
        if (statusSink_) statusSink_(msg, priority);
    }
```

Instance size grows by ~32 bytes (empty `std::function` on xtensa). RAM impact is
negligible against the ~205 KB internal SRAM budget (CLAUDE.md `## Common Pitfalls`
invariant: no PSRAM on StampS3 Cardputer).

### Priority levels (ADR-0164)

Defined as plain `uint8_t` constants (no enum class, to keep the `emitStatus` signature
trivial and because the values never cross a type boundary):

```cpp
// src/modules/capture/handshake_capture.h, near emitStatus
constexpr uint8_t STATUS_PRIO_IDLE   = 1;  // Waiting for EAPOL M3...
constexpr uint8_t STATUS_PRIO_LOW    = 2;  // Client seen, QoS-Null, deauth burst
constexpr uint8_t STATUS_PRIO_MED    = 3;  // PMKID solicit, BTM steer, EAPOL M2-M4
constexpr uint8_t STATUS_PRIO_HIGH   = 4;  // PMKID captured, Handshake captured
```

### Screen-side buffer and overlap policy

Add to `HandshakeScreen`:

```cpp
private:
    char statusMsg_[48] = {0};       // 32-char cap + " · " + NUL headroom
    uint8_t statusPrio_ = 0;
    uint32_t statusExpiryMs_ = 0;    // millis() when dwell ends; 0 = empty

    void onStatusEmit(const char* msg, uint8_t priority);
```

`onStatusEmit` runs on the capture task (not the UI task). It must only copy the
string into `statusMsg_` and set the expiry millis; no draw calls, no SD, no mutex
contention (ADR-0164 "Bad" note: a slow sink callback blocks capture).

Overlap policy, implemented in `onStatusEmit`:

```cpp
void HandshakeScreen::onStatusEmit(const char* msg, uint8_t priority) {
    const uint32_t now = millis();
    const bool dwellExpired = (now >= statusExpiryMs_);

    // Higher priority preempts immediately.
    // Same-or-lower waits for dwell expiry, then newest wins.
    if (priority > statusPrio_ || dwellExpired) {
        strncpy(statusMsg_, msg, sizeof(statusMsg_) - 1);
        statusMsg_[sizeof(statusMsg_) - 1] = '\0';
        statusPrio_ = priority;
        const uint32_t dwellMs = (priority >= STATUS_PRIO_HIGH) ? 4000 : 2000;
        statusExpiryMs_ = now + dwellMs;
    }
    // else: drop. The sink has room for exactly one pending string; mid-dwell
    // same-or-lower messages are dropped by design, serial log keeps the trace.
}
```

### Render-site edit

Current `handshake_screen.cpp` (slice-0163 state, line ~1748):

```cpp
canvas.setCursor(4, y);
canvas.setTextColor(theme::TEXT_SECONDARY());
canvas.printf("%lus remaining", remaining);
```

New:

```cpp
canvas.setCursor(4, y);
canvas.setTextColor(theme::TEXT_SECONDARY());
canvas.printf("%lus remaining", remaining);
if (statusMsg_[0] != '\0' && millis() < statusExpiryMs_) {
    canvas.print(" \xC2\xB7 ");  // " · " middle dot, UTF-8
    canvas.print(statusMsg_);
}
```

The status half clips naturally at the canvas edge: on Cardputer (240 px) the full
line including a 17-char MAC fits; on M5Stick (160 px) the trailing MAC bytes clip,
which ADR-0164 accepted as correct degradation (countdown must remain intact).

**Render clears stale content**: when `statusMsg_[0] != '\0'` but
`millis() >= statusExpiryMs_`, the status half renders nothing. On the next
`onStatusEmit`, the buffer is overwritten. We do not zero `statusMsg_` on
expiry (unnecessary work on the UI task; the guard above handles it).

### The 11 emit callsites and the serial-log audit

Each row below is the single moment the sink fires for that event, with the current
serial-log state. The implementing change wires the sink next to the existing log,
or adds a log where none exists.

| # | File / moment | Sink string | Prio | Existing log? |
|---|---|---|---|---|
| 1 | `handshake_capture.cpp` client discovered | `Client XX:XX:XX:XX:XX:XX` (full MAC) | 2 | AUDIT |
| 2 | `handshake_capture.cpp` QoS-Null fire path (1 client) | `QoS-Null to 1 client` | 2 | AUDIT |
| 3 | `handshake_capture.cpp` QoS-Null fire path (N clients) | `QoS-Null to N clients` | 2 | AUDIT |
| 4 | `handshake_capture.cpp` deauth burst sent | `Deauth burst (N frames)` | 2 | AUDIT |
| 5 | `handshake_capture.cpp` PMKID solicit sent | `PMKID solicit sent` | 3 | AUDIT |
| 6 | `handshake_capture.cpp` PMKID captured | `PMKID captured` | 4 | AUDIT |
| 7 | `handshake_capture.cpp` BTM Request sent | `BTM steer sent` | 3 | AUDIT |
| 8 | `handshake_capture.cpp` EAPOL M2 seen | `EAPOL M2 seen` | 3 | AUDIT |
| 9 | `handshake_capture.cpp` EAPOL M3 seen | `EAPOL M3 seen` | 3 | AUDIT |
| 10 | `handshake_capture.cpp` EAPOL M4 seen | `EAPOL M4 seen` | 3 | AUDIT |
| 11 | `handshake_capture.cpp` 4-way complete | `Handshake captured` | 4 | AUDIT |

Plus one screen-side idle fallback (priority 1), emitted from `HandshakeScreen`
itself when `autoState_ == ATTACKING`, dwell has expired, and M1 or M2 has been
seen but not M3:

- `Waiting for EAPOL M3...` (prio 1)

AUDIT at implementation time: grep each callsite for an existing `log_i` / `log_w`
/ `LOG_D` matching the moment, and only add a new log where none exists. No sink
output is logged twice.

### Considered shapes (ADR-0164 re-states)

- **EventBus**: rejected. Needs a new `EventType`, a payload struct, subscription
  bookkeeping, and crosses a module boundary nothing else needs to cross. The
  screen is the only consumer.
- **Polling getter**: rejected. Loses moments shorter than UI refresh (~100 ms) and
  requires every event source to stash a timestamped "last message" string. Push
  is cheaper.
- **Direct observer `std::function` sink**: chosen. One pointer, one setter, one
  lifetime tied to the screen's `onEnter` / `onExit`.

### Existing code considered

- `HandshakeCapture::getFirstDiscoveredClient()`, `getDiscoveredClientCount()`
  (slice-0163): the client the sink reports on event #1 is drawn from the same
  discovery path. No new discovery machinery.
- `handshake_screen.cpp` render path: the status line hooks into the existing
  `canvas.printf("%lus remaining", ...)` line without a layout change.
- `log_i` / `log_w` / `LOG_D` macros already exist at most capture-engine moments;
  the audit uses them rather than inventing a new logger.
- No reuse of EventBus: considered, rejected (above). The one-to-one shape is
  correct here.

## Verification

Unit test (native Unity, extends the existing autohunt scoring test suite in
`test/native/`):

- `test/native/test_rolling_status.cpp`:
  - `test_single_message_dwells_for_full_period`: push one prio-2 message at t=0,
    assert the buffer reports it at t=1999ms, assert it clears at t=2001ms.
  - `test_higher_priority_preempts_mid_dwell`: push prio-2 at t=0 (2s dwell),
    push prio-4 at t=500ms, assert the buffer reports the prio-4 string and its
    expiry is t=500+4000ms (not 0+2000).
  - `test_same_priority_drops_mid_dwell`: push prio-2 at t=0, push a different
    prio-2 at t=500ms, assert the buffer still shows the first string and its
    expiry is still t=2000.
  - `test_same_priority_replaces_after_dwell`: push prio-2 at t=0, push prio-2
    at t=2500ms, assert the buffer shows the newer string and expiry=t=4500ms.
  - `test_lower_priority_drops_mid_dwell`: push prio-4 at t=0, push prio-2 at
    t=500ms, assert the prio-4 string and its 4s expiry survive.
  - `test_32_char_truncation`: push a 40-char string, assert the stored buffer is
    32 chars + NUL and the longest field it carries is intact at that cut.

The native test fixture injects `millis()` via a parameter (the production code
calls `millis()` directly; the test harness wraps `onStatusEmit` in a thin shim
that takes an explicit clock argument). This is the same clock-injection shape
used by slice-0162's backoff tests.

Field verify (Cardputer) once native is green:

- Run autohunt in a live lab for 5 minutes with intent=Both. Confirm by on-screen
  reading that each of these strings was observed at least once during the run
  (serial log cross-references each): `Client ..`, `QoS-Null to N client(s)`,
  `Deauth burst (N frames)`, `PMKID solicit sent`, one of `EAPOL M2/M3/M4 seen`,
  and `Waiting for EAPOL M3...` (if any attack window hits the quiet-after-M2
  state). `PMKID captured` and `Handshake captured` are opportunistic and may not
  fire in a 5-minute run; a serial trace of a matching log line is acceptable
  evidence for the prio-4 path if the capture does not complete live.

## As built

Shipped per the design above with two adjustments.

**Shape split by actual callsite location**: ADR-0164 listed 11 engine moments
inside `handshake_capture.cpp`, but QoS-Null (#2/#3) and BTM steer (#7) actually
live in `handshake_screen.cpp` (slice-0155, slice-0156, slice-0160 all put the
TX call in the screen's update loop, not in the capture engine). The sink on
`HandshakeCapture` ended up wiring 8 of the 11 moments (client discovered,
deauth burst, PMKID solicit, PMKID captured, EAPOL M2/M3/M4 seen, handshake
captured twice: once at grace-expiry CRACKABLE and once at updateCaptureState
COMPLETE); the remaining 3 (QoS-Null auto, QoS-Null manual, BTM auto, BTM
clientless) call `HandshakeScreen::onStatusEmit` directly. Same RollingStatus
buffer, same overlap policy, two emit entry points instead of one. The sink
is still cleared in the dtor and `hide()` so a background capture cannot
write into a destroyed screen.

**Pure helper extracted**: the policy lives in a new header
`src/ui/components/rolling_status.h` (ADR-0164's priority + dwell + 48 B cap in
a POD `RollingStatus` with an `emit(msg, prio, now)` method), so it is
native-testable without pulling `HandshakeScreen` into the native build. The
screen holds one instance and `onStatusEmit` delegates.

**Idle fallback**: wired in `updateAutoHunt()`'s ATTACKING case, gated on
`hasMsg1||hasMsg2 && !hasMsg3 && !rollingStatus_.visible(now)`, re-emitted at
most every 5000 ms so it does not blow out higher-priority bursts. Anchor
`lastIdleFallbackMs_` clears at NEXT_TARGET alongside the per-target BTM / QoS
latches.

**Serial parity audit**: every sink emit lands next to an existing log line.
Client discovered uses the pre-existing `LOG_D` in `addDiscoveredClient`;
EAPOL M2/M3/M4 use the generic `LOG_D("EAPOL %s from...")` already emitted at
the top of `processEAPOL`; deauth burst, PMKID solicit, PMKID captured, and
handshake complete all have their own `LOG_I` next door. No new logger was
introduced.

**Verification run**:
- `pio test -e native` on 2026-11-21: 1162/1162 tests pass, including the new
  9-test suite in `test/test_rolling_status/test_rolling_status.cpp` (clock
  injected per emit; covers dwell, higher-priority preempt, same/lower-priority
  drop mid-dwell, same-priority replace after expiry, 47-char truncation,
  prio-1 idle drop-vs-fill, reset).
- `pio run -e cardputer`: SUCCESS, RAM 25.3% (82880 / 327680), Flash 76.0%
  (2541835 / 3342336).
- `pio run -e m5stick`: SUCCESS, RAM 20.9% (68344 / 327680), Flash 79.5%
  (2500679 / 3145728).
- Field verify 2026-11-21 (Cardputer, autohunt intent=Both, ~5 min in a live
  area): each sink string observed at least once on screen alongside the
  serial trace, including `Client AA:BB:CC:DD:EE:FF`, `Deauth burst (N frames)`,
  `EAPOL M2 seen`, `EAPOL M3 seen`, and the prio-1 `Waiting for EAPOL M3...`
  gap-filler. The full MAC + separator + countdown fit the Cardputer 240 px
  canvas with no visible clipping.

LEDGER line 28 ("Autohunt UI rolling status line") deleted in the same commit;
the v0.11.0 roadmap row marks the item shipped.
