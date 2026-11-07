---
id: '0145'
title: "A 4-way handshake is complete only when M1 and M2 share a client and replay counter"
type: architecture
status: accepted
date: 2026-11-07
supersedes: []
superseded_by: []
---

## Context

`HandshakeCapture` accumulates EAPOL messages into one per-BSSID struct
(`handshake_`). `processMessage1/2/3/4` store the latest frame of each type keyed
only on the target BSSID; when `captureAllClients` is set (the default for broad
capture and Auto Hunt) there is no check that M1 and M2 belong to the same
4-way exchange. M1's client MAC is not even retained (only M2/M4 write
`handshake_.clientMac`), and replay counters are never read.

This produces invalid handshakes whenever more than one exchange is in the air,
and the clientless-PMKID feature makes it routine: the device solicits an M1 by
associating with a random `02:..` STA, and if the AP returns M1 without a PMKID
KDE, that solicited M1 (addressed to our fake STA) gets paired with an ambient
real client's M2 from an unrelated handshake. The resulting `.pcap` carries
frames from two different stations with mismatched replay counters, and the
synthesised hashcat line pairs an ANonce and a MIC that were never part of the
same exchange.

Measured on a field card (2026-11-07), three captures WPA-SEC rejected with
"no valid handshakes":

| File | M1 | M2 | same exchange? |
|------|----|----|----------------|
| DIGIFIBRA-A3Hm | rc=2, to `02:ad:5a:11:ed:01` (solicited) | rc=1, from `f4:30:b9:b5:20:d5` (real client) | no |
| MERCUSYS_C718 | rc=2 | rc=3 | no |
| LME-PRINTER | rc=1 | rc=1 (plus a stray M4 rc=2) | marginal |

A valid WPA 4-way has M2 replying to M1 with the *same* replay counter, both
between the *same* AP and *same* STA. This is the correctness criterion
WPA-SEC/hcxtools apply; the device must apply it too before it claims a
handshake, writes a `.pcap`, or synthesises a `.22000`.

This is distinct from the hashcat 22000 type-tag inversion fixed alongside it
(`hc22000_format.h`): that mislabelled otherwise-valid lines; this is about not
assembling an invalid capture in the first place.

## Decision

A 4-way handshake is treated as complete (savable, countable, exportable) only
when its M1 and M2 (or M2 and M3) demonstrably belong to the same exchange:

1. **Same station** - the client MAC of the ANonce message (M1, or M3) must
   equal the client MAC of the MIC message (M2). Retain M1's client MAC, not
   only M2's.
2. **Corresponding replay counters** - M2's replay counter must equal M1's
   (M1/M2 pair), or M4's must equal M3's (M3/M4 pair). Store the replay counter
   of each stored message.

When the stored messages do not satisfy this, the capture is incomplete: do not
write the `.pcap`, do not synthesise the `.22000`, and do not count it. A
solicited M1 with no PMKID and no matching M2 from the same STA yields nothing,
which is correct.

Implementation (field retention, the pairing check, and where it gates
`isMinimumValid`/the save path) lands in its own slice with its own regression
tests; this ADR fixes the completeness rule those tests assert against.

## Consequences

- Captures that today "succeed" by mixing frames from different stations will
  correctly stop being saved. The on-device success/PMKID counters will drop to
  reflect real captures; this is a truer number, not a regression.
- `CapturedHandshake` grows a client MAC for the ANonce source and a per-message
  replay counter (small, fixed cost).
- Full correctness in a crowded RF environment ultimately wants per-client
  handshake tracking (multiple concurrent exchanges on one BSSID). This ADR
  mandates the pairing *invariant*; a per-client accumulator is a possible later
  refinement and is out of scope here.
- Existing mislabelled/invalid captures already on SD cards are unaffected; this
  governs new captures. Re-capturing the affected APs produces valid handshakes.
