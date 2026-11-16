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

Shipped as two commits on `feature/slice-0156-btm-steering-clientless`:

- `8d70fcc docs(slice): BTM steering parity for Clientless PMKID mode (slice-0156)` —
  this document, INDEX regenerated. No LEDGER line to delete (the Clientless parity
  item was carved from the slice-0155 handoff discussion, not from a ledger entry).
- `ea82b7d feat(clientless): one-shot BTM steer in the Clientless capture window
  (slice-0156)` — Clientless config form gains a third row `BTM steer (unverified):
  On|Off` between Target and START; `startCapture()` resets the per-capture latch
  and anchors the 10 s delay timer; the screen's `update()` tick consults the pure
  slice-0155 predicate with `pmfRequired` and `armsSolicitation` pinned true and on
  fire transmits a frame whose candidate BSSID is the target BSSID with the LAA bit
  flipped, latches, and logs the `BTM:injected` marker.

Per-DoD run-through:

1. **Clientless + steer on + 10 s elapsed with no PMKID → one BTM Request + log
   marker.** Met. The slice-0155 predicate returns true once per capture when
   `elapsed >= CLIENTLESS_BTM_DELAY_MS` with the latch down; `sendBtmRequest(…)`
   runs on STA; the `LOG_I("HS", "%s: BTM:injected (unverified, candidate=... ch=%u)
   tx=%s", …)` goes out in the same tick.
2. **Steer off → no injection.** Met. The default initializer `clientlessBtmSteer_
   = false` holds; the predicate returns false when `steerEnabled` is false; the
   only flip site is `handleConfigEdit` on Clientless row 1.
3. **At most one injection across a full capture run.** Met. `btmInjectedClientless_`
   is latched to true before the TX call so a TX failure cannot loop, and it is
   cleared only on the next `startCapture()`.
4. **Row label contains "(unverified)".** Met. `drawConfig`'s Clientless branch
   renders `ui::ConfigRow::render(c, rowY, "BTM steer (unverified):", …)`; one
   call site, matches the slice-0155 wording verbatim.

Verification evidence:

- `pio test -e native` — 1107/1107 pass (same count as the slice-0155 baseline;
  the Clientless cell falls inside the five-input truth table already covered by
  `test_btm_frame`).
- `pio run -e cardputer` — clean, flash 76.6% (2.56 MB of 3.34 MB).
- `pio run -e m5stick` — clean, flash 79.5% (2.50 MB of 3.15 MB).

Follow-ups inherited from slice-0155 (not new, listed for completeness):

- On-device evidence of whether a spoofed BTM Request actually survives PMF on any
  real stack. The `(unverified)` label stays until this runs. Clientless is
  arguably the better verification venue than Auto Hunt, since the lack of a
  deauth path makes any observed client roam attributable to BTM.
- Multi-BSS decoy to back the candidate BSSID (today it is a fabricated LAA MAC
  with nothing behind it).
- Native integration test around the live call site (both Auto Hunt and Clientless
  call sites pull in `WiFi.*` and Arduino, so the predicate is tested but the
  full fire path is not).

## Amendment — 2026-11-16: ADR-0157 drops the `(unverified)` suffix

After lab-verifying both Auto Hunt (slice-0155) and Clientless (this slice) BTM steer on
Cardputer ADV — knob behaves, frame transmits, log trace lands — ADR-0157 partially
supersedes ADR-0153's label clause. The `(unverified)` UI suffix is dropped from both
config rows and from the `BTM:injected` log line in the same commit as this amendment.

Effect on the frozen prose above: DoD scenario #4 and the per-DoD run-through for #4 both
assert the literal `(unverified)` string. Those lines are historical claims met at this
slice's merge (ADR-0019 immutability); the current UI no longer satisfies them because
ADR-0157 amends the requirement for all future UI and log sites.

The open question that originally motivated the suffix — does a spoofed BTM Request
actually survive PMF on any real client stack? — is unchanged, and moves to a new LEDGER
`[deferred]` entry naming the client-stack matrix (iOS, Android, modern Linux supplicant)
and the two possible next moves (confirmed-on-any-stack → follow-up ADR records the
matrix; rejected-everywhere → rename the knob to `Legacy BTM (non-PMF only)` or revert
both slices).

