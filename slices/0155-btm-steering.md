---
id: '0155'
title: "802.11v BTM steering for PMF-required targets (opt-in, unverified)"
type: slice
status: accepted
date: 2026-11-15
supersedes: []
superseded_by: []
---

## Goal

Ship the BTM half of the LEDGER entry that was split out of ADR-0153 when slice-0154 shipped
the PMF-aware selection half. Give autohunt one extra stimulation path on PMF-required targets
where deauth is a dead end: a spoofed 802.11v BSS Transition Management Request (WNM action
frame) naming a bogus candidate BSSID, injected once per target when the PMKID solicitation
window's first half elapses without yielding an M1. The honest open question (whether a
spoofed BTM survives PMF on real stacks) stays loudly labelled `(unverified)` in the UI until
a device run proves it one way or the other. Closes the "802.11v BTM steering for
PMF-required targets" LEDGER item.

## Definition of Done

- **Given** a target with `pmfRequired=true`, BTM steer toggle on, autohunt strategy that
  arms PMKID solicitation
- **When** the solicitation window's first half elapses with no PMKID captured
- **Then** the device injects exactly one BTM Request frame whose source/BSSID is the target
  AP, whose destination is broadcast, whose WNM action body sets Preferred Candidate List
  Included and Disassociation Imminent, and whose Neighbor Report element lists the
  configured candidate BSSID; and the hunt log line for that target gains a `BTM:injected`
  marker.

- **Given** a target with `pmfRequired=false` and BTM steer toggle on
- **When** autohunt runs its window
- **Then** no BTM Request is injected (policy skip: the deauth or solicitation paths already
  work there and BTM adds nothing) and the hunt log line carries no BTM marker.

- **Given** the BTM steer toggle off (the default)
- **When** autohunt runs any target
- **Then** no BTM Request is ever injected, regardless of PMF state.

- **Given** the Auto Hunt config form rendered on either device
- **When** the operator scrolls to the BTM row
- **Then** the row label contains the literal string `(unverified)` so the knob never claims
  a capability the device has not demonstrated.

- **Given** a byte-level native test of the pure frame builder
- **When** the builder is called with a fixed dialog token, candidate BSSID, disassociation
  timer, and target BSSID
- **Then** the returned buffer starts with an 802.11 management header for a WNM Action
  frame (FC type=mgmt, subtype=action), carries Category=10 and Action=7, has the request
  mode byte with bits 0 (Preferred Candidate List Included) and 2 (Disassociation Imminent)
  set, encodes the timer little-endian, and appends a tag-52 Neighbor Report IE whose body
  starts with the exact 6-byte candidate BSSID.

## Design

**What existed and was considered.** The deauth module (`src/modules/attack/deauth.h/.cpp`)
already owns a static pure frame builder (`buildDeauthFrame`) tested natively, and private
TX plumbing (`sendRawFrame` on the ESP32 side). The pattern is the right one to copy: a pure
builder in a new header lives under `src/utils/` so native tests exercise the bytes without
mocking the radio, and a thin TX helper calls `esp_wifi_80211_tx` the same way deauth does.
No deauth code is reused in the new path: BTM is a different frame class (WNM action, not
deauth), with its own addressing (broadcast, so a single transmission reaches every
associated client of the target) and its own policy (one-shot per target, not a burst). The
pmf_resolver's `EffectiveStrategy` is read but not extended: BTM is a tactical stimulator
that runs *alongside* a PMKID-arming strategy, not a fifth strategy of its own, so the
resolver stays with its four values.

**Frame layout.** The WNM BSS Transition Management Request body (after the 24-byte
management header) is:

```
Category        1 byte   = 10 (WNM)
Action          1 byte   = 7  (BSS Transition Management Request)
Dialog Token    1 byte   (nonzero, caller-chosen)
Request Mode    1 byte   bit0 = Preferred Candidate List Included
                         bit1 = Abridged
                         bit2 = Disassociation Imminent
                         bit3 = BSS Termination Included
                         bit4 = ESS Disassociation Imminent
Disassoc Timer  2 bytes  little-endian, TUs until disassoc (0 = none)
Validity Intvl  1 byte   number of beacon intervals the candidate list stays valid
[Neighbor Report IE(s)]  tag 52, carrying the candidate BSSID(s)
```

The Neighbor Report IE (tag 52) body is BSSID(6) + BSSID Info(4) + Operating Class(1) +
Channel Number(1) + PHY Type(1), optionally followed by subelements. For our use the
subelements are omitted (keeps the TX envelope tight and the builder unit-testable).

**Policy wiring.** The handshake_screen autohunt FSM already latches per-target state at
LOCKING (slice-0154). One extra latched field is enough: `btmInjectedThisTarget_` (bool),
set when the one-shot fires, cleared at `NEXT_TARGET` with the other per-target state. The
fire condition is checked in the existing `ATTACKING` tick, not a new state: when the window
timer passes its midpoint, `btmInjectedThisTarget_` is false, `btmSteerEnabled_` is true,
the latched target is `pmfRequired`, and the latched effective strategy includes
solicitation (so a PMKID could still land) — then build one frame and send it. Logging the
injection uses the existing hunt-log facility; the marker is one word so the line stays
within budget.

**Candidate BSSID.** The frame names a candidate BSSID the AP is telling its clients to
move to. In this slice we use a fixed locally-administered MAC derived by flipping the LAA
bit of the target BSSID (so the field looks plausible without conflicting with any real
upstream MAC). No fake AP is stood up to back it. The honest expectation: a client that
blindly honours the BTM will lose its current association reaching for a BSS that is not
there, and in reassociating to the real AP it issues the 4-way we want. Standing up a real
decoy BSS (karma-style) is a separate ledger item (CSA injection and multi-BSS decoys land
their own ADRs); this slice stays to one TX.

**Why default off.** The frame is honest-labelled `(unverified)` because 802.11v BTM
Requests are robust management frames: on paper PMF protects them, so a spoofed BTM should
fail the exact same way spoofed deauth does under MFPR. In practice some stacks have been
reported to honour unprotected BTM against the spec. Shipping it off by default means the
feature is available for the operator who wants to run the experiment on a known lab
target; shipping it with the `(unverified)` suffix in the UI means the device never claims
a capability it has not demonstrated.

**Non-scope.** CSA injection (separate LEDGER item), QoS-Null Data stimulation (separate
LEDGER item), and the per-target scoring / backoff work (separate LEDGER item) are out.
Multi-BSS decoy to actually back the candidate BSSID with a karma-style AP is out.
Verification that the frame changes any real client behaviour is out: this slice ships the
knob and the TX; verification is called out in `## As built` as a tracked follow-up and does
not block the slice from merging. That gate is the same shape slice-0017 (CC1101 jamming)
used for code-complete-but-RF-unverified.

## Verification

- `pio test -e native -f test_btm_frame` — a new native test module asserting:
  - Builder returns the expected buffer length for a request mode with two bits set and a
    single Neighbor Report subelement.
  - Management-header bytes: FC type=mgmt (0x00), FC subtype=Action (0xD0), the three
    address fields (dest=broadcast, src=BSSID, BSSID=BSSID).
  - WNM body bytes: Category=10, Action=7, Dialog Token echoed back, Request Mode has the
    expected bit mask, Disassoc Timer is little-endian, Validity Interval echoed.
  - Neighbor Report IE: tag=52, length matches the body, first 6 body bytes equal the
    candidate BSSID verbatim.
  - Policy fail-before/pass-after guard: a stubbed decision function
    `shouldInjectBtm(pmfRequired, btmSteerEnabled, btmAlreadyInjected, halfWindowElapsed,
    effectiveArmsSolicitation)` returns true only in the one cell that matches the DoD
    scenario and false in every other cell. (The real call site in handshake_screen reads
    the same predicate so the condition stays native-testable.)
- `pio test -e native` — full suite stays green.
- `pio run -e cardputer` and `pio run -e m5stick` — both build (serial, per CLAUDE.md).
- On-device (lab): flash Cardputer ADV, start an autohunt with BTM steer on against a
  known-PMF target, confirm from the serial trace that one BTM Request is emitted at the
  half-window mark and never again for that target; confirm the hunt-log line carries the
  `BTM:injected` marker; confirm that a non-PMF target in the same hunt never sees an
  injection. Field-verification of whether the frame is honoured under PMF on any real
  stack is recorded as a follow-up in `## As built` (the whole point of the `(unverified)`
  label).

## As built

<!-- Filled in at merge. -->
