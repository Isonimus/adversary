---
id: '0138'
title: "Clientless PMKID as-built: blind-pair solicitation, PMKID-event save, third top-level mode"
type: architecture
status: accepted
date: 2026-11-04
supersedes: []
superseded_by: []
---

## Context

ADR-0137 chose clientless PMKID via injected association (Option B) and gated the whole
feature behind an on-device feasibility spike. The spike **passed on 2026-11-04**: against a
known PMKID-emitting AP (`LKS_Invitados`, `14:ab:02:01:a7:d2`), an injected open-auth +
association-request pair from a spoofed STA MAC (`02:ad:5a:11:ed:01`) drew an EAPOL M1
carrying a PMKID KDE, `recordPmkid` stored it keyed on the spoofed STA MAC, and the saved
`WPA*02` `.22000` line cracked against the known PSK. All three legs of the acceptance
condition held. The feature is therefore cleared to build.

Building it surfaced three design questions ADR-0137 either sketched one way or did not
answer. ADR-0137's prose is immutable (it recorded the gate and the pre-spike plan), so the
as-built refinements are recorded here. This ADR does not supersede ADR-0137: Option B and
its acceptance condition stand unchanged; this records how the proven path is shaped into
production.

## Decision

### 1. Blind-pair solicitation, not await-response

ADR-0137's design sketch was "inject auth-request, await auth-response, inject
association-request, await association-response and M1." The spike did not await either
response: it injected the open-auth frame, waited a fixed ~25 ms, injected the
association-request, and repeated on an interval, with the promiscuous RX path picking up M1
whenever it arrived. That blind pair is what passed on-device.

**Ship the blind pair.** The production solicitation is: inject open-auth, wait ~25 ms,
inject association-request (RSN IE advertising CCMP + AKM PSK), repeat every solicit
interval, bounded to N attempts, stop immediately when a PMKID is recorded. Add RX-side
auth/association-response gating only if a future AP is found that refuses the blind pair.

Rationale: the blind pair is the measured-correct mechanism (it drew M1 on the real chip and
IDF on 2026-11-04). Response-parsing in the promiscuous callback is unproven-necessary code;
adding it now would be speculative generality against a path we have never seen fail. The
~25 ms inter-frame delay stays inline (a short blocking wait once per solicit interval)
because it is the measured gap that worked and is shorter than the main loop's redraw
cadence, so splitting it across ticks would only stretch it. This is KISS plus
measured-data-over-estimate.

### 2. Save and on-screen count are driven by the PMKID event, not the 4-way save path

The spike showed the entire save-and-count pipeline is coupled to a 4-way handshake:
`saveHandshake()` calls `saveHandshakeToPcap`, which bails at `isMinimumValid()` (needs M2 +
one of M1/M3), and the on-screen PMKID counter is incremented only inside that success
branch. A clientless PMKID is M1 + a PMKID KDE with no M2, so it never reaches the counter
and was saved only because the spike wrote the `.22000` directly.

**The clientless path persists and counts off `PMKID_CAPTURED`.** `recordPmkid` already
publishes that event, but from the promiscuous RX callback, which runs in the Wi-Fi driver
task where SD I/O is unsafe (CLAUDE.md: defer callback work to a task). So the screen's
subscriber only flags the capture; the screen's main-loop `update()` then persists it via
`handshake_save::savePMKIDToFile` (which writes the `WPA*02` line and registers the capture)
and increments the on-screen PMKID count. The 4-way `saveHandshake()` path is untouched and
remains the only writer for 4-way captures. The two paths must not be merged: a PMKID-only
capture and a 4-way capture have different minimum-valid criteria and different save writers.

### 3. A third top-level handshake mode, not a toggle inside Manual

Clientless PMKID is a third peer mode (`HandshakeMode::Clientless`) alongside Auto Hunt and
Manual, reached from the Wireless -> Handshake submenu, fixed at construction like the other
two (ADR-0094: mode is not a runtime toggle; each mode's ENTER acts on its selected row). It
is not a toggle inside Manual.

Rationale: a Manual toggle would muddy two things that differ by mode. Success criteria
differ (a PMKID KDE from the AP vs an M2 from a client), and the save/count semantics differ
(per decision 2). Collapsing both into one screen with a mode flag would fork nearly every
config-form and capture branch on that flag, which is exactly the coupling ADR-0094 split
Auto Hunt and Manual to avoid. The Clientless config form is minimal: a Target row (ENTER
routes to the scanner, reusing the shared `(select from scanner)` placeholder) and START.
There is no deauth option, because the point of this mode is to solicit M1 without deauthing
anyone (ADR-0137).

## Consequences

- The solicitation frame builders (open-auth and association-request) move from the throwaway
  spike header to `src/utils/` as pure functions with native byte-exact regression tests,
  per ADR-0137's "keep the new logic pure and host-tested like `pmkid_utils`." The spike
  header and its `update()` hook are deleted once promoted.
- The bounded state machine lives in `HandshakeCapture`, armed by a new
  `solicitPmkid` config flag; it reuses the softAP TX netif and promiscuous RX already
  present, adds no radio interface, and does not disturb the passive 4-way path. On no PMKID
  after N attempts it reports a timeout through the existing state, so the screen's timeout
  handling is reused.
- The deauth-forced passive path (v0.7.2) stays as the fallback for APs that do not answer
  solicitation. Clientless is additive, not a replacement.
- The hashcat STA MAC in this path is our injected MAC (correct for this path), which differs
  from the passive path where it is the real client. The two paths keep separate save
  writers so that distinction cannot be lost.
