---
id: '0156'
title: "BTM steering parity for Clientless PMKID mode (opt-in, unverified)"
type: slice
status: accepted
date: 2026-11-16
supersedes: []
superseded_by: []
---

## Goal

Extend the slice-0155 one-shot BTM Request injection to Clientless PMKID mode (ADR-0138),
so the "quiet client, bring them out" story has a stimulation vector of its own instead of
only waiting for the solicit retry loop to land an M1. Clientless is the one mode where
BTM has the clearest value add: no deauth-force-4way path is available, the solicit loop
runs for ~30 s against an AP with no visible clients, and a BTM Request has at least a
chance of nudging a dozing client into a reassoc exchange we can catch as a 4-way. Same
honest `(unverified)` label as slice-0155 since the open question of whether a spoofed
BTM survives PMF is unchanged.

## Definition of Done

- **Given** a Clientless capture with BTM steer toggle on
- **When** 10 s have elapsed since capture start without the solicit path yielding a PMKID
- **Then** the device injects exactly one BTM Request against the configured target
  (source/BSSID = target AP, destination = broadcast, request mode =
  PreferredCandidateListIncluded | DisassociationImminent, candidate BSSID = target BSSID
  with the LAA bit flipped), and the capture log line gains a `BTM:injected` marker.

- **Given** the BTM steer toggle off (the default)
- **When** a Clientless capture runs for its full duration
- **Then** no BTM Request is ever injected.

- **Given** the BTM steer toggle on and the capture ran for its full duration
- **When** the capture window elapses or the target is retargeted
- **Then** at most one BTM Request was transmitted, never a burst.

- **Given** the Clientless config form on either device
- **When** the operator scrolls to the BTM row
- **Then** the row label contains the literal string `(unverified)`, same as the Auto Hunt
  row shipped by slice-0155.

## Design

**What existed and was considered.** Slice-0155 shipped the pure WNM BTM Request builder
(`utils::buildBtmRequestFrame`), the ESP32 TX helper (`utils::sendBtmRequest`), and the
pure injection predicate (`utils::shouldInjectBtm`). All three are reused verbatim here:
Clientless parity is a new UI row plus a new fire site, not a second frame builder or a
second predicate. This is why the slice is small on purpose.

**Why reuse `shouldInjectBtm` instead of a Clientless-shaped predicate.** The existing
predicate's five inputs collapse to the Clientless cell by holding two constants:
`pmfRequired = true` (Clientless treats BTM as allowed regardless of the target's PMF bit:
Clientless's whole premise is that clients are quiet, not that the AP is rejecting
management frames, so the PMF gate from Auto Hunt does not apply) and `armsSolicitation =
true` (Clientless is always solicitation-only, ADR-0138). What remains is `steerEnabled`,
`alreadyInjected`, and a "delay elapsed" flag that stands in for Auto Hunt's "half-window
elapsed." The predicate stays one pure function; the two call sites just feed it
different constants for the fields they do not care about. Native tests from slice-0155
already cover the five-input truth table; no new predicate tests are added.

**Why 10 s delay and not half-window.** Clientless has no wall-clock window: the solicit
loop is bounded by `SOLICIT_MAX_ATTEMPTS * SOLICIT_INTERVAL_MS = 20 * 1500 ms ≈ 30 s`
inside `HandshakeCapture`. A "half-window" idiom has no anchor to attach to. 10 s is a
deliberate first-third: lets the solicit loop send ~6 blind pairs before BTM shakes the
tree, and leaves ~20 s for a reassoc + 4-way to land after the frame goes out. The number
lives as one constant in the screen; no config surface for it.

**Where it fires.** Clientless captures land on `HandshakeScreenState::CAPTURING` in
`handshake_screen.cpp`. The screen's `update()` tick already calls `capture_.update()`
for non-Auto-Hunt modes; a sibling check reads the clock, feeds `shouldInjectBtm`, and
on fire builds the frame + calls `sendBtmRequest(…)`, latches
`btmInjectedClientless_`, and logs the marker. The latch is cleared at `startCapture()`
so a re-run of the same target gets a fresh injection chance.

**Non-scope.** Manual mode's BTM parity: deliberately out, same reasoning as the
slice-0155 handoff discussion (Manual exists for operators who want fine-grained control;
anyone wanting BTM on one target can run a one-target Auto Hunt or Clientless). Field
verification that the frame actually causes any client to roam: same open question as
slice-0155, tracked there.

## Verification

- `pio test -e native` — full suite stays green (no new tests: the predicate is the one
  shipped and covered by `test_btm_frame` in slice-0155; the Clientless call site holds
  two of the five inputs constant and the cell is already in that truth table).
- `pio run -e cardputer` and `pio run -e m5stick` — both build (serial, per CLAUDE.md).
- On-device (lab): flash Cardputer ADV, open Handshake → Clientless, confirm the new
  `BTM steer (unverified): Off|On` row renders between Target and START, toggle it on,
  pick a PMF-required target, start capture, confirm from the serial trace that exactly
  one BTM Request is emitted at the ~10 s mark and never again for that capture; confirm
  the log line carries the `BTM:injected` marker; confirm that a capture run with the
  toggle off emits no injection at all. Field-verification of whether the frame is
  honoured under PMF is the slice-0155 follow-up; this slice inherits it.

## As built

<!-- Filled in at merge. -->
