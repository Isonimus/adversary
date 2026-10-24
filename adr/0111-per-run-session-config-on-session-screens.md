---
id: '0111'
title: "Per-run session params live as a session-screen quick-config over the canonical Settings field"
type: architecture
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Context

`WirelessSettings` (`src/modules/storage/settings_manager.h:39`) is the single persisted
source of truth for every scan and attack parameter: karma channel/rotation, deauth burst
and interval, beacon and probe-flood intervals, scan timeout, wardriving scan interval, and
wardriving radio mode (`wardrivingRadioMode`, line 50). Session screens read these fields
live rather than owning their own copies: the wardriving screen reads `wardrivingRadioMode`
every `update()`, and the handshake idle screen tells the operator outright "Uses values
from Settings menu" (`src/ui/screens/handshake_screen.cpp:1070`). One editor, one store, no
duplication: that is the invariant worth keeping.

The friction this decision resolves is reach, not storage. The single most run-contextual
knob is wardriving's radio mode (WiFi / BLE / Both). It is chosen from the RF environment in
the moment right before a walk, yet it sits two levels deep in the global Settings screen
(`settings_screen.cpp:117`), so the operator must leave the wardriving screen, scroll to the
Wardriving section, set it, back out, re-enter, and start. Scan interval (line 116) has the
same property. The obvious fix (put a selector on the wardriving idle screen) creates a
tension the project has so far avoided: moving one knob onto one screen while handshake still
points at Settings is a one-off inconsistency, and the lazy version of it (copy Settings
fields onto screens) would fork the source of truth and reintroduce exactly the manual-sync
bug the single-store design exists to prevent.

So the real question is not "should wardriving get a selector"; it is "what is the rule for
where a parameter is edited", decided once so wardriving, handshake, and anything later all
follow it instead of each screen inventing its own answer.

## Decision

A session screen may carry an inline **quick-config** for the small set of parameters that
are *per-run* by the predicate below. The quick-config is a UI affordance only: it reads and
writes the canonical `WirelessSettings` field directly through `SettingsManager`, and the
global Settings screen remains the full editor of that same field. There is no second store
and no screen-local copy of the value. A value set from a session screen persists through the
normal settings save path and shows correctly in the Settings editor; a value set in Settings
shows correctly on the session screen. The two are the same field viewed through two
affordances, never two fields kept in sync.

A parameter qualifies as per-run, and is therefore eligible for a session-screen quick-config,
only when all of these hold:

1. It is chosen from the physical or RF environment at the moment of starting a session, not
   a stable operator preference (wardriving radio mode: "what is around me right now";
   wardriving scan interval: "how fast am I walking"). 
2. Changing it between runs, or at the start of a run, is a normal part of the workflow.
3. It is already a persisted `WirelessSettings` field. The quick-config introduces no new
   state; if a knob would need new persistence, that is a separate decision, not this one.
4. The session screen has an idle / pre-start state where the selector fits without crowding
   the live-status view.

Parameters that fail the predicate stay Settings-only. Stable preferences (keyboard layout,
fixed BLE MAC, BLE broadcast name, cap-detect override, IR TX source) are set once and are
not per-run, so they never earn a session-screen selector. This predicate is the guard that
keeps "quick-config on the session screen" from degrading into "Settings duplicated
everywhere".

## Consequences

- **v0.6.0 applies it to wardriving.** The wardriving idle screen gains a radio-mode
  selector (and scan interval is a candidate by the same predicate), writing through to
  `wireless.wardrivingRadioMode` / `wardrivingScanIntervalMs`. The live-read the screen
  already does keeps working unchanged, because the field it reads is the field the selector
  writes.
- **handshake is the next consistent application, not a contradiction.** Its idle screen's
  "Uses values from Settings menu" copy marks it as the screen that has not adopted the
  pattern yet. When its per-run knobs (scan timeout, channel) get a quick-config, they follow
  this same rule rather than a new ad-hoc design. The inconsistency is resolved by committing
  to one pattern, not by holding every knob in Settings. Tracked as a ledger follow-up citing
  this ADR.
- **The single-source-of-truth invariant is strengthened, not weakened.** The decision
  explicitly forbids a screen-local copy: the one failure mode (two values drifting apart) is
  designed out, and fail-loud applies if a future screen is tempted to cache the value instead
  of binding to the field.
- **Cost:** each adopting screen grows a small selector bound to the live field and must route
  edits through `SettingsManager` so they persist. That is a per-screen UI addition, bounded
  by the predicate to the handful of genuinely per-run knobs, not a framework.
- Settings remains the complete, canonical editor of all parameters, including the per-run
  ones. The quick-config is a shortcut to a subset, never a replacement.
