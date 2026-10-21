---
id: '0094'
title: "Split handshake capture into peer Auto Hunt and Manual screens"
type: architecture
status: accepted
date: 2026-10-21
supersedes: []
superseded_by: []
---

## Context

The handshake feature ships two genuinely different operations behind one screen and one
mutable `autoHuntMode_` bool (`handshake_screen.h:177`). They are not two configurations of
one flow:

- **Auto Hunt** is a scan -> lock -> attack -> wait state machine (`AutoHuntState`,
  `handshake_screen.h:62`) with its own capturing render (`drawAutoHuntCapturing`) and its
  own in-capture keys (`w` = whitelist-and-skip, `h` = view session list,
  `handshake_screen.cpp:280-311`). It finds its own targets.
- **Manual** is target -> three options (auto-deauth, deauth count, timeout) -> capture,
  and takes its target from the scanner.

Three problems follow from fusing them:

1. **The flagship is undiscoverable.** The root menu lists a single `"Handshake Capture"`
   leaf (`menu_factory.cpp:23`). Auto Hunt, one of the firmware's headline features, is
   reachable only by pressing `M` *inside* the screen (`handshake_screen.cpp:204`). An
   operator who never presses `M` never learns it exists.
2. **The toggle hides a surprising interaction.** Pressing ENTER on Auto Hunt's row 0
   silently reverts the screen to Manual (`handshake_screen.cpp:228-231`), with no footer
   hint. The mode is mutable state the operator can flip by accident.
3. **The fused layout blocks UI convergence.** The selectable item count changes with mode
   (2 in Auto Hunt, 5 in Manual: `handshake_screen.cpp:214-215`). This mode-varying count,
   together with the interleaved non-selectable info lines and the bespoke Target row, is
   the exact measured reason recorded in `LEDGER.md` for excluding handshake from the
   uniform-row (`ScrollableList` / future `ConfigRow`) convergence, and the reason
   ADR-0092 lists handshake as an interaction exception.

## Decision

**Present the two operations as peer menu entries, and fix the mode at launch instead of
toggling it at runtime.**

- **Navigation.** Add a nested submenu: `Wireless -> "Handshake" -> { "Auto Hunt",
  "Manual Capture", Back }`, built with the existing `MenuItem::submenu` primitive
  (`menu_factory.cpp` already nests `Wireless` itself the same way). Auto Hunt and Manual
  become peers, side by side.
- **Topology.** Keep **one** `HandshakeScreen` implementation. The `CAPTURING` and
  `SUCCESS` states, the session handshake list, and the capture module are genuinely shared
  between the two flows; splitting the class would duplicate them. Fix the mode as an
  immutable `HandshakeMode` set at construction, exposed as **two `ScreenId`s**
  (`HANDSHAKE_AUTO`, `HANDSHAKE_MANUAL`) wired to two factories
  (`new HandshakeScreen(HandshakeMode::...)`) and two rows in the action->screen route
  table. Delete the mutable `autoHuntMode_` toggle, the `M`/`m` key, and the hidden
  ENTER-revert.
- **Target hand-off.** The scanner's `publishAttackTarget(ScreenId::HANDSHAKE, ...)`
  (`scanner_screen.cpp:295`) targets `HANDSHAKE_MANUAL`; Auto Hunt finds its own targets
  and never arrives from the scanner.

**Considered and rejected: the RFID one-shot static launch-intent flag**
(`RfidScreen::requestWriteMode()` / `s_writeModeRequested`, `rfid_screen.h:29`,
`rfid_screen.cpp:73`, dispatched at `main.cpp:785`). That pattern lets two menu actions
share one `ScreenId` by setting a static bool the screen consumes on entry. It fits RFID
because the dashboard is the primary flow and write is an optional sub-flow off it. It does
not fit handshake: Auto Hunt and Manual are co-equal peers, not primary-plus-deviation, and
reusing it would add a second mutable launch-intent global (the project pitfalls say to
minimize global state). Two `ScreenId`s express "two peers" directly in the route table and
the registry, type-safely, with no hidden one-shot state.

## Consequences

- **Each entry has a fixed selectable count.** Auto Hunt becomes an info + `START` screen
  (an `ActionButton` target, not a config form). Manual becomes a uniform config form:
  Target + three option rows + `START`, whose Target row routes to the scanner exactly like
  `evil_twin` row 0 (slice-0093). This removes the mode-varying-count objection recorded in
  the ledger and makes Manual a clean `ConfigRow` target, which is the next slice in the
  convergence arc.
- **The ADR-0092 handshake interaction exception is dissolved.** Manual now conforms to the
  canonical model (ENTER advances a value row; the Target navigation row routes to the
  scanner), and Auto Hunt is not a config form, so the model simply does not apply to it.
  ADR-0092's prose stands as historically accurate for the fused screen; this ADR records
  that exception as moot from here on.
- **One extra navigation hop** to reach either capture flow. Judged worth it: it surfaces
  the flagship and removes the accidental-toggle hazard. ESC/Back from either screen returns
  to the new Handshake submenu, not the root menu.
- **Net code effect:** +1 `ScreenId`, +1 menu action, two factories, one submenu. In
  exchange the screen's render / update / input lose their per-mode branches, and the
  confusing `M` toggle and ENTER-revert are deleted, so the screen gets simpler where it
  was most error-prone.
