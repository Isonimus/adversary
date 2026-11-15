---
id: '0153'
title: "PMF-aware attack selection and 802.11v BTM steering"
type: architecture
status: accepted
date: 2026-11-13
supersedes: []
superseded_by: []
---

## Context

Autohunt today treats every target identically: deauth-force the AP for a fixed window, and
save any 4-way or opportunistic PMKID that lands. ADR-0139 layered a per-hunt Strategy
(Deauth, PMKID, Both) on top, but the selection is still blind to the one capability of the
target that makes deauth either work or silently fail: Protected Management Frames.

PMF is the RSN IE's RSN Capabilities field, bit 6 `MFPR` (required) and bit 7 `MFPC` (capable).
Mandatory in WPA3, opt-in in modern WPA2. When MFPR is set, the AP drops any unprotected
management frame that claims the station's address, which is exactly how our deauth injection
and any future spoofed CSA-in-beacon work. The AP does not reply, the client does not reconnect,
the window elapses, and the next target is tried. From the operator's point of view autohunt
"did something" against the target, but no capture was ever reachable through that path.

The scanner receives the RSN IE as part of beacon/probe-response processing but does not cache
PMF capability or requirement on the scan record. Autohunt has no way to make an informed
choice, and the on-screen scanner UI gives the operator no indication that a given AP is out
of reach of deauth-style attacks.

PMKID solicitation (ADR-0137, ADR-0138) is unaffected by PMF: the M1/EAPOL-start exchange it
induces is a data frame carrying EAP, not a management frame, and the response is the normal
RSN-authenticated EAPOL-M1 the AP sends to any requesting client. Solicitation is therefore the
natural degraded path for PMF-required targets when the chosen strategy includes it.

802.11v BSS Transition Management is a WNM action frame in the robust management class.
Whether a spoofed BTM Request survives PMF is an open question: on paper PMF protects robust
management frames (BTM qualifies), so spoofed BTM should fail the same way spoofed deauth
does. In practice some stacks have been reported to honour unprotected BTM against the
spec. The honest answer is "mark it as unverified until the device sees it work."

## Decision

**Cache PMF bits on the scan record and consult them in autohunt target selection.**

Scan-record extension:

- Extend the discovered-network struct with `bool pmfCapable` and `bool pmfRequired`, parsed
  once from the RSN IE at discovery and carried through the normal scan-results flow. No
  runtime cost beyond two bits per network; no new frame parsing (the RSN IE is already in
  hand for the encryption-type field).
- Scanner UI shows a compact PMF marker on rows where `pmfRequired` is set. The marker is
  advisory and non-blocking: the operator can still target the AP manually, but autohunt
  will not waste cycles on deauth there.

Autohunt effective-strategy resolution:

- The user-chosen Strategy (ADR-0139) becomes a declared intent, not a per-target command.
  At each `NEXT_TARGET` transition the device resolves an **effective strategy** from the
  intent and the target's PMF bits:
    - `pmfRequired == false`: effective strategy equals the intent, unchanged.
    - `pmfRequired == true` and intent includes solicitation (`PMKID` or `Both`): effective
      strategy is `PMKID` only. Deauth is skipped for this target.
    - `pmfRequired == true` and intent is `Deauth`: effective strategy is `PMKID` if the
      "degrade to PMKID on PMF" toggle is set (default on), otherwise the target is skipped
      with reason `PMF_DEAUTH_INEFFECTIVE`. Advancing past the target rather than burning
      the window is the whole point.
- The autohunt screen surfaces the effective strategy on the per-target line (`Deauth`,
  `PMKID`, `PMKID (PMF)`, `Skipped (PMF)`), so the operator can see when the device is
  degrading rather than obeying.

BTM steering (opt-in, honest):

- Add a WNM BTM Request **frame builder** and an Auto Hunt knob labelled `BTM steer
  (unverified)`, default off. When on and the target is `pmfRequired` and solicitation has
  not yielded a PMKID within the window's first half, inject a BTM Request naming a candidate
  BSSID that resolves to our own fake AP. A client that honours it attempts a transition and
  issues the association exchange we can capture.
- The label stays `(unverified)` until on-device evidence shows a spoofed BTM is accepted
  under PMF on any real AP/client pair. Once verified or ruled out, this ADR gets a follow-up
  or an amendment. Honest-labelling, not over-claiming, is the whole gate.
- BTM injection is skipped on `pmfRequired == false` targets: the deauth or solicitation
  paths already work there, and BTM adds nothing to a non-PMF hunt.

Scope boundary:

- CSA injection (a separate ledger item) reuses the same `pmfRequired` gate when it lands:
  unprotected beacon-carried CSA is useless under PMF; action-frame CSA is a robust management
  frame and shares BTM's open-question status. A sibling ADR covers CSA when its slice begins;
  this ADR records the policy for PMF-aware selection, not the frame bestiary.
- Autohunt per-target scoring and exponential backoff (sibling ledger item) is orthogonal
  and gets its own ADR. Scoring may read the `pmfRequired` bit as one input among others, but
  the backoff policy is behaviorally independent of the PMF gate and does not belong here.

## Consequences

- Scan record grows by 2 bits per network. Scanner, saved-networks, and the on-disk scan
  snapshot (if any) carry the bits through unchanged except for serialization.
- The capture strategy selector (ADR-0139) grows an **effective-strategy** concept: the user's
  declared intent and the device's per-target resolution are now distinct, and the hunt log
  must record the resolved strategy, not just the intent, so an after-action review sees
  where PMF degraded the hunt.
- PMKID solicitation becomes the load-bearing clientless path on modern WPA2/WPA3 networks.
  Any future regression in solicitation now bites a larger class of targets than before,
  which is a reason to keep its fail-loud instrumentation (ADR-0138) intact and not relax it.
- Autohunt stops silently no-opping on PMF-required deauth-only hunts: with the default
  `degrade to PMKID` toggle on, operators get solicitation reach they did not have before.
  With the toggle off, they get a legible `Skipped (PMF)` status instead of a quiet window
  burn.
- The BTM knob is a deliberate "unverified" surface. If the field pass shows BTM is rejected
  under PMF on every real stack tested, the knob either becomes `Legacy BTM (non-PMF only)`
  or disappears. The device never claims a capability it has not demonstrated.
- No new dependency on the precompiled wpa_supplicant archive: PMF parsing is a 2-bit read
  off the RSN IE already in hand, and BTM is a frame we build and inject, not something we
  ask the supplicant to send. The owned-ESP-IDF initiative (ledger item under Build
  reproducibility) stays decoupled from this work.
