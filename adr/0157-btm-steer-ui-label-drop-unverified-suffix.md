---
id: '0157'
title: "BTM steer UI label: drop the \"(unverified)\" suffix, keep the verification question in the LEDGER"
type: architecture
status: accepted
date: 2026-11-16
supersedes: []
superseded_by: []
---

## Context

ADR-0153 ("PMF-aware attack selection and 802.11v BTM steering") set the honesty gate
for the BTM steer knob with one specific line:

> The label stays `(unverified)` until on-device evidence shows a spoofed BTM is accepted
> under PMF on any real AP/client pair. Once verified or ruled out, this ADR gets a
> follow-up or an amendment.

That clause shipped in slice-0155 (Auto Hunt) and slice-0156 (Clientless parity). The
two UIs now render `BTM steer (unverified): On|Off` in their config forms, and the
`LOG_I("AUTOHUNT"/"HS", "BTM:injected (unverified, candidate=... ch=%u) tx=%s", …)`
trace carries the same hedge.

What ADR-0153 bought at the time: one word of honesty on a surface the operator reads,
in exchange for a certain amount of visual noise and a label the operator has to parse
every time they see the row. The gate was explicitly named ("verified or ruled out,
this ADR gets a follow-up or an amendment"), so this is that amendment.

What changed between ADR-0153 and now:

- slice-0155 shipped. The pure frame builder is byte-level tested (13 unit tests pin
  every header, the WNM Category=10 / Action=7 body, the request-mode bit mask, the
  little-endian disassoc timer, and the tag-52 Neighbor Report IE). The ESP32 TX helper
  rides the already-exercised `RawWiFi::transmit` path. The predicate is tested at
  every cell. Lab-flashed on Cardputer ADV, the config row renders, the knob behaves,
  the frame transmits.
- slice-0156 shipped the Clientless parity. Same builder, same TX helper, same
  predicate with two inputs pinned; UI row renders, same verification posture.
- The "does the frame survive PMF on a real stack" question is not resolved by any of
  the above. It remains open.

What has *not* changed: the underlying unknown. We still do not have on-device evidence
that a spoofed BTM Request is honoured by a real client under PMF. The feature is
honest to the operator who knows what BTM is; the suffix was meant to protect an
operator who might not.

What is uncomfortable about keeping the suffix now: the UI label is the only place the
honesty gate lives, which conflates two different things the operator's reading mind
has to disentangle every pass over the row:

1. "Does this feature do what its name says?" (It does: it transmits a BTM Request.
   That part is not unverified.)
2. "Will that frame actually cause any client to roam under PMF?" (That is the open
   question.)

The suffix reads as a hedge on (1), which is misleading in the wrong direction: the
toggle is not a half-finished feature, it is a verified knob whose downstream
effectiveness against a specific class of targets is unproven. That is a LEDGER entry,
not a UI hedge.

## Decision

**The UI label becomes `BTM steer:` in both the Auto Hunt and Clientless config
forms.** The `(unverified)` suffix is dropped. The `BTM:injected (unverified, ...)`
log trace drops the parenthetical for the same reason: the log is operator-facing
too, and the hedge belongs in exactly one place.

The verification question itself is not dropped: it moves to `LEDGER.md` as a tracked
`[deferred]` item with the specifics ADR-0153 named:

- Lab-flash a BTM-steer run against a known PMF-required target.
- Observe whether any associated client issues a reassoc request or a 4-way exchange
  within the capture window that would not have happened without the frame.
- Repeat across a small matrix of client stacks (iOS, Android, modern Linux
  supplicant) since BTM acceptance varies by implementation.
- If the frame is rejected on every stack tested, name BTM as `Legacy BTM (non-PMF
  only)` in a follow-up ADR, or revert both slice-0155 and slice-0156.
- If the frame is accepted on any stack, record the matrix of what did and did not
  honour it in a follow-up ADR, and leave the label as is.

Partial supersession: this ADR supersedes only ADR-0153's "the label stays
`(unverified)` until..." clause. Every other policy ADR-0153 set (PMF-caching in the
scan record, PMKID as the degraded path, the one-shot-at-half-window rule for Auto
Hunt, the pmfRequired gate for Auto Hunt BTM) is unchanged.

## Consequences

- The slice-0155 DoD scenario #4 and slice-0156 DoD scenario #4 both require the
  literal `(unverified)` string in the UI. Both DoDs are now historical claims:
  they were met at merge, and this ADR amends the requirement going forward. The
  slice `## As built` sections are the audit trail; neither is rewritten (ADR-0019
  immutability).
- The LEDGER gains one entry that owns the honest verification work. The device
  can still ship the feature; the operator reads one word less; the open question
  is still answerable the moment the lab pass runs.
- Future work on BTM (CSA follow-up, multi-BSS decoy, or the "Legacy BTM only"
  rename if the field pass goes the wrong way) cites this ADR as the governing
  decision, not ADR-0153's label clause.
- If the field pass eventually confirms BTM does not survive PMF on any stack
  tested, the knob either becomes `Legacy BTM` (non-PMF targets only) or disappears
  entirely. Nothing about dropping the suffix today commits us to the current
  policy; it only moves the hedge off the UI and onto the ledger.
