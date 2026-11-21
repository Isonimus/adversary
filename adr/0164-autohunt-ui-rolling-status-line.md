---
id: '0164'
title: "Autohunt UI rolling status line"
type: architecture
status: accepted
date: 2026-11-21
supersedes: []
superseded_by: []
---

## Context

Autohunt (ADR-0161) runs a lot of invisible work inside the ATTACKING phase: QoS-Null
stimulation (ADR-0160, slice-0160), deauth bursts, PMKID solicitation, BTM steering
(ADR-0155, ADR-0156), EAPOL M1-M4 tracking, and the handshake-capture promiscuous
listener (slice-0163). The autohunt screen today only shows the four coarse state
labels (SCANNING / LOCKING / ATTACKING / WAITING), a progress bar, the "Xs remaining"
countdown, and the EAPOL M1/M2/M3/M4/PMKID indicator strip. Everything else sits in
the serial log.

Slice-0163's field pass made the gap concrete: the attack-window listener saw a client
nobody watching the device could see, with no on-screen trace. When autohunt leaves a
weak target and moves to a stronger one, the operator has no way to know whether the
run was quiet because the engine did nothing or because the attack provoked nothing;
the only signal is the final score comparison in the "NEXT_TARGET" transition, which
is a serial log line.

The engine already emits every interesting event internally (`HandshakeCapture`
publishes client discoveries, QoS-Null counts, deauth bursts; BTM and PMKID paths log
their own moments). What is missing is a one-line sink that surfaces the most recent
one to the screen.

## Decision

Add a one-line rolling status string to the autohunt screen, rendered **inline**
after the "Xs remaining" countdown on the same row (a space economy choice: the
Cardputer has a 135 px vertical budget and the EAPOL strip wants the next line).

### Emitter shape: direct observer on HandshakeCapture

`HandshakeCapture` holds an optional `std::function<void(const char*, Priority)>`
status sink. `HandshakeScreen` sets the sink at `onEnter()` (autohunt mode only)
and clears it at `onExit()`. The capture engine, the QoS-Null stimulator (which
lives inside the capture flow), and the BTM/PMKID paths that `HandshakeCapture`
calls into invoke the sink directly. No EventBus traffic, no payload type, no
subscription lifecycle: one function pointer, one callsite set per screen.

Rejected alternatives:
- **EventBus with a new event type**: needs an event enum entry, a payload struct,
  subscription/unsubscription bookkeeping, and crosses a module boundary that nothing
  else needs to cross. Screen is the only consumer; one-to-one observer is the right
  shape.
- **Poll a getter each `update()`**: loses moments shorter than the UI refresh
  (~100 ms), and forces every event source to also stash its own "last message"
  string with a timestamp. Push is cheaper.

### Event set

The engine emits the following strings via the sink (all lowercase label with
capitalized first word, in English; no punctuation at the end):

| Event (code moment) | Rendered string | Priority |
|---|---|---|
| Client discovered on channel | `Client 02:AD:5A:11:ED:01` | 2 (low) |
| QoS-Null stimulation fired | `QoS-Null to 1 client` / `QoS-Null to N clients` | 2 (low) |
| QoS-Null skipped: no client | (not emitted; too noisy) | - |
| Deauth burst sent | `Deauth burst (N frames)` | 2 (low) |
| PMKID solicited | `PMKID solicit sent` | 3 (medium) |
| PMKID captured | `PMKID captured` | 4 (high) |
| BTM Request sent | `BTM steer sent` | 3 (medium) |
| EAPOL M1 seen | (not emitted; EAPOL strip shows it) | - |
| EAPOL M2 seen | `EAPOL M2 seen` | 3 (medium) |
| EAPOL M3 seen | `EAPOL M3 seen` | 3 (medium) |
| EAPOL M4 seen | `EAPOL M4 seen` | 3 (medium) |
| 4-way handshake complete | `Handshake captured` | 4 (high) |
| Waiting for a message | `Waiting for EAPOL M3...` | 1 (idle, fallback) |

String length is capped at **32 chars** (the sink truncates with a trailing `...`
if longer). Horizontal budget: `"15s remaining · "` eats ~96 px of the 240 px wide
Cardputer canvas at the default 8 px font, leaving ~144 px which fits ~18 default
chars or more with a slightly smaller face. If the combined line overflows, the
status half is clipped (not the countdown), since the countdown is load-bearing
for the state-timeout invariant.

### Overlap policy: priority-with-dwell

Two rules, in order:

1. **Higher priority replaces any current message immediately.** A `Handshake
   captured` (prio 4) wins over an in-dwell `Client 02:...` (prio 2) even if the
   client message landed 100 ms ago.
2. **Same-or-lower priority replaces only once the current message has served its
   full dwell.** Within a priority level, newest wins after dwell expiry. Before
   expiry, newer same-priority messages are dropped (not queued): the sink has
   room for exactly one pending string, and the operator reads one event at a
   time.

**Dwell** is **2000 ms** for priorities 2 and 3, and **4000 ms** for priority 4.
Rationale: 2 s is long enough to notice and read 18-25 chars on a 1.14" screen
without feeling sticky; the 4 s on priority-4 (capture success) gives the
operator a real chance to see the moment the hunt paid off before the next
NEXT_TARGET transition re-triggers rendering.

### Render contract

Rendered at `handshake_screen.cpp` where `"%lus remaining"` is drawn today
(line ~1748 at authoring), with a `" · "` separator after the countdown and
the status string joined after it. Style: `theme::TEXT_SECONDARY()` for both
halves. No blinking, no color per priority (priority already picks which
string wins; coloring would add visual noise for no new information).

When autohunt is **not in ATTACKING** state, the status half is empty: SCANNING
and LOCKING do not emit (the countdown and the state label already explain the
screen), and WAITING renders `Waiting for EAPOL M3...` only if M1/M2 were seen
earlier in the same attack window.

### Serial parity

Every sink emit point also has a serial log line in the same code path, so the sink
is debuggable without a camera on the Cardputer screen. **The sink itself does not
log**: most of the 11 emit points already write a serial line today (QoS-Null fires,
deauth bursts, PMKID solicitation, EAPOL progression, handshake completion all log),
and routing them all through a sink-side `LOG_I` would double-print them. The
implementing slice (0165) audits each callsite: if a matching serial line already
exists, the sink is added next to it and no log is touched; if one is missing, the
slice adds one in the same path. The sink is purely a UI surface, not a logger.

## Consequences

**Good**:
- The engine's invisible work becomes visible to an operator watching the screen.
- Single consumer, single setter, single lifetime (`onEnter`/`onExit`): nothing to
  leak across screens.
- The 32-char cap and 2-4 s dwell bound how much screen time any one event can
  demand, so a chatty burst (e.g. 5 QoS-Null fires in 200 ms) collapses to one
  visible string plus 4 drops, not a flicker storm.
- Serial parity means the sink is self-documenting from the log.

**Mitigated**:
- Dropping same-or-lower priority messages during dwell loses information in dense
  bursts. Accepted: a burst of 5 same-priority events in <2 s carries no actionable
  distinction for the operator, and the serial log preserves every drop verbatim.
- Priority-2 `Client` strings show a full MAC (17 chars) plus 7 chars of label.
  That pushes the combined line to the clip threshold on the stick (240 px canvas
  on Cardputer, 160 px on M5Stick). Accepted for Cardputer (fits); on M5Stick
  the status half clips and we lose the trailing MAC bytes, which is a correct
  degradation (state-timeout invariant > identity detail).

**Bad**:
- Adds a `std::function` member to `HandshakeCapture`, growing its instance size
  by ~32 bytes (empty std::function on xtensa). RAM impact: negligible against
  the ~205 KB internal SRAM budget.
- The sink runs on the capture task; a slow sink callback blocks capture. Screen's
  implementation must only copy the string into a ring-sized buffer and set a flag;
  no draw calls, no SD, no mutex contention. The screen renders from the buffer at
  its own cadence in `render()`.

## Implementation (slice follows)

A slice **0165** will implement:
- `HandshakeCapture::setStatusSink(std::function<void(const char*, uint8_t)>)`
  and `clearStatusSink()`.
- Priority enum (2/3/4) and a 48-byte string buffer on the screen, with a
  monotonic expiry millis field.
- The ~11 emit callsites listed above, each audited against the existing serial
  log: callsites that already log get the sink next to the existing line and no
  new log; callsites missing a log get one added in the same path.
- Render-site edits in `handshake_screen.cpp` around the "Xs remaining" line.
- A native Unity test for the priority-with-dwell policy (fixture: push a
  sequence of strings with simulated millis, assert the buffer content).
- Field verify: run autohunt in a live environment, confirm every event kind
  is observed at least once over a 5-minute run.
