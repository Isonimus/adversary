---
id: '0139'
title: "Auto Hunt capture-strategy selector: deauth, PMKID solicitation, or both"
type: architecture
status: accepted
date: 2026-11-04
supersedes: []
superseded_by: []
---

## Context

ADR-0138 shipped clientless PMKID solicitation as a separate focused top-level mode, and
deliberately kept it out of the other handshake flows: a single-target mode's success, save
and count semantics must stay legible (a PMKID KDE from the AP is a different artifact from a
4-way M2 from a client), so Manual and Clientless stay distinct.

Auto Hunt is the opposite kind of mode. It is multi-target, fire-and-forget, and already
"save whatever we capture": it walks a channel list, deauth-forces each target for a fixed
window, and saves any 4-way or opportunistic PMKID it sees. Its one capture method today is
deauth, which can only produce a handshake when a client is present (or reconnects). A huge
class of targets, PMK-caching APs with no associated client, yield nothing to a deauth hunt
but would answer injected solicitation with an M1 PMKID. Operators want that reach during a
hunt, and want the option to do both per target.

## Decision

Add a per-hunt **Strategy** to the Auto Hunt config: **Deauth** (current behaviour), **PMKID**
(solicit only, no deauth), or **Both** (solicit and deauth in the same per-target window).
This does not contradict ADR-0138: the focused single-target modes stay exactly as they are;
Auto Hunt, the capture-anything mode, is the natural home for "which method(s) to try across
targets," and adding the selector there muddies nothing those focused modes guard.

Mechanism:

- Solicitation becomes a **parallel frame injector** in `HandshakeCapture`, armed by the
  existing `solicitPmkid` config flag. Each target window it injects the proven blind pair
  (ADR-0138), bounded to the same attempt count, and stops injecting on the first PMKID or
  when the attempts are exhausted. It no longer owns the capture's terminal state.
- The terminal state is owned by context: in the pure clientless case (solicit on, deauth
  off, i.e. the focused Clientless mode and the PMKID strategy) the solicitation result is the
  whole outcome (PMKID -> CRACKABLE, exhausted -> TIMEOUT). In Deauth and Both the normal
  4-way/deauth flow and Auto Hunt's window timer own it, with solicitation layered on top.
- In Auto Hunt, a solicited PMKID is saved and counted off `PMKID_CAPTURED` and then advances
  to the next target: a PMKID is a crackable win for that AP, so there is no reason to burn
  the rest of the window. Deauth-strategy PMKIDs (a PMKID KDE seen on a deauth-forced M1) keep
  flowing through the existing 4-way `saveHandshakeToPcap` writer exactly as before.
- **Double-save is prevented by the per-target `handshakeSaved_` guard** already reset on each
  `NEXT_TARGET`: both the PMKID writer and the 4-way writer check it, so whichever lands first
  for a target wins and the other is skipped. The two writers are never both run for one
  target.

Naming: the strategy row is labelled `Strategy:` with values `Deauth`, `PMKID`, `Both`, matching
the on-screen PMKID counter wording.

## Consequences

- Auto Hunt gains its first config value row (it was a single START button). The config form,
  its input classifier, and its render grow from one row to Strategy + START.
- The handshake screen subscribes to `PMKID_CAPTURED` in every mode but gates the handler by
  mode and strategy: the focused Clientless mode, and Auto Hunt only when the strategy uses
  solicitation. Manual and Deauth-strategy Auto Hunt never take the event path, so their save
  behaviour is unchanged.
- The deauth-only hunt path is byte-for-byte the prior behaviour (solicitation is skipped when
  `solicitPmkid` is false), so the change is additive and the default strategy stays Deauth.
- Both runs solicitation and deauth concurrently. Because solicitation usually draws a PMKID
  within a second or two, Both behaves as "PMKID-first, deauth as the fallback that keeps
  working for the rest of the window if no AP answers solicitation."
