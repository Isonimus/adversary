---
id: '0123'
title: "Resolve the handshake quick-config follow-up: no per-run field to surface, fix stale Auto Hunt copy"
type: slice
status: accepted
date: 2026-10-25
supersedes: []
superseded_by: []
---

## Goal

Close the ADR-0111 follow-up "apply the per-run quick-config pattern to the handshake idle
screen". Measured against the code, the pattern has no valid target on this screen, so the
resolution is to record that finding and fix the one real defect it exposed: the Auto Hunt
idle screen's "Uses values from Settings menu" line, which is false.

## Definition of Done

- **Given** the ADR-0111 handshake follow-up ledger item
- **When** this slice lands
- **Then** the item is removed from the ledger and its resolution (no quick-config, copy fix)
  is recorded here.

- **Given** the Auto Hunt idle screen
- **When** it is rendered
- **Then** it no longer claims to use Settings values; the copy states what the mode actually
  does (runs on built-in timing, honors the whitelist).

- **Given** the change is display copy only
- **When** the suite and both device builds run
- **Then** the native count is unchanged (no logic added) and both device builds are green.

## Design

ADR-0111 (Consequences) named handshake's per-run knobs as "scan timeout, channel" without
checking the Auto Hunt path. None of that survives contact with the code:

- **Auto Hunt reads no tunable from Settings.** Its timing is four compile-time constants in
  `handshake_screen.h` (`AUTO_SCAN_TIME`, `AUTO_LOCK_TIME`, `AUTO_ATTACK_TIME`,
  `AUTO_WAIT_TIME`); the per-target deauth is hard-coded (`deauthCount=5`, `autoDeauth=true`)
  in the LOCKING path. The only Settings input it consults is the **whitelist**
  (`isWhitelisted` in `selectNextTarget`), which protects matching APs from being targeted.
- **There is no operator-chosen channel.** Auto Hunt scans all channels and locks to each
  discovered target's own channel (`targetChannel_`, set per target), so a channel knob is
  meaningless here.
- **ADR-0111's own predicate rules the follow-up out.** Predicate #3 requires the knob to be
  an already-persisted `WirelessSettings` field introducing no new state; binding or
  persisting Auto Hunt's local constants is, by that predicate, "a separate decision, not this
  one". The pattern has nothing to bind to on this screen.
- **Manual mode is out of scope too.** Its config rows (`timeoutSecs_`, `autoDeauth_`,
  `deauthCount_`) are screen-local, ephemeral per-capture inputs, not views on a persisted
  field, so ADR-0111 (which governs a persisted field seen through two affordances) never
  applied to them.

Decision: do **not** add a handshake quick-config. The pattern stays applied where it fits
(wardriving, slice-0112). The defect the investigation exposed is the Auto Hunt idle screen's
"Uses values from Settings menu" copy: false, since Auto Hunt ignores those Settings. Replace
it with copy that matches the measured behavior (built-in timing, whitelist honored). This is
`drawConfig`'s Auto Hunt branch, a static `canvas.print` sequence.

## Verification

- Display-copy change only: the edited block adds no branch or computation, so there is
  nothing host-testable to regress and the native suite stays at its current count. The build
  is the gate, exactly as for other render-only copy changes.
- `pio test -e native`: unchanged count, green.
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS (serial, -j 2 for the
  host OOM ceiling).

## As built

As designed. No handshake quick-config was added; the ADR-0111 follow-up is closed as
resolved-by-investigation (the pattern has no persisted per-run field to bind to on this
screen). The only code change is the Auto Hunt idle copy in `handshake_screen.cpp`
`drawConfig`: the false "Uses values from Settings menu" line is replaced by "Scans, targets,
and captures handshakes automatically." + "Skips whitelisted networks.", which matches the
measured behavior (built-in `AUTO_*_TIME` timing, whitelist the only Settings input). The
ledger item under UX / reliability is deleted.

Verification run this change:
- `pio test -e native`: 920/920, unchanged count (no logic added, as expected for display copy).
- `pio run -e cardputer -j 2`: SUCCESS (RAM 26.5%, Flash 75.5%, identical to slice-0122).
- `pio run -e m5stick -j 2`: SUCCESS (RAM 21.0%, Flash 78.4%, identical to slice-0122). Zero
  flash delta confirms the change is copy-only.
- Docs: `build-index.mjs` + `lint-docs.mjs` green (122 documents).
