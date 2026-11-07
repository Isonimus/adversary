---
id: '0146'
title: "Pair EAPOL messages by client MAC and replay counter"
type: slice
status: accepted
date: 2026-11-07
supersedes: []
superseded_by: []
---

## Goal

Stop `HandshakeCapture` from assembling a 4-way handshake out of EAPOL messages that belong
to different exchanges. Implements the completeness rule from ADR-0145: a capture counts as
valid only when its ANonce message (M1/M3) and MIC message (M2) share a client MAC and
corresponding replay counters. This removes the invalid captures WPA-SEC rejects as "no
valid handshakes" (confirmed on a field card: our clientless-PMKID solicitation M1 was being
paired with an unrelated client's M2).

## Definition of Done

- **Given** an M1 addressed to one station and an M2 from a different station (the
  clientless-PMKID solicitation case), or an M1/M2 pair carrying mismatched replay counters
- **When** the capture is evaluated for saving
- **Then** `isMinimumValid()` and `isWpaSecValid()` are false, and the capture is not saved,
  counted, or exported.

- **Given** an M1 and M2 from the same station with identical replay counters (or an M2/M3
  pair from one station whose M3 replay counter is one past M2's)
- **When** the capture is evaluated
- **Then** `isMinimumValid()` is true, and `isWpaSecValid()` is true once a beacon is also
  present, so a genuine handshake still saves exactly as before.

- **Given** the completeness predicate is reverted to the old presence-only check
- **When** the pairing regression test runs
- **Then** the mismatch cases fail (proving the test guards the invariant), and all cases
  pass with the fix in place; the full native suite and both device builds stay green.

## Design

The pairing data lives on `CapturedHandshake` so the checks are pure and unit-testable
off-device (the capture class itself needs ESP32 Wi-Fi APIs):

- New fields: `msg1Client/msg2Client/msg3Client[6]` and `msg1Replay/msg2Replay/msg3Replay`
  (8-byte big-endian EAPOL-Key replay counter, read at key-frame offset 5).
- `processMessage1/2/3` record the station (already extracted for client targeting) and the
  replay counter via a shared `readReplayCounter()` helper. `processMessage2/3` gained a
  `clientMac` parameter (M1 already had one).
- `hasM1M2Pair()` / `hasM2M3Pair()` encode the invariant; `hasValidPair()` is their OR, and
  `isMinimumValid()`/`isWpaSecValid()` are expressed in terms of them.

`isValid()` (all four messages present) stays presence-based: it is the UI/deauth-stop
"ideal full capture" signal, and the authoritative save gate is `isMinimumValid()` in
`saveHandshakeToPcap`, which now rejects an unpaired capture regardless.

Out of scope (per ADR-0145): per-client handshake tracking for multiple concurrent exchanges
on one BSSID, and the rare case where M2 replies to an uncaptured M1 that happens to share a
replay counter (not detectable on-device without the PMK). The three field files remain
invalid and need re-capture.

## Verification

- `pio test -e native -f test_handshake_pairing`: 7 cases, proven fail-before (3 pairing
  cases fail on the old `hasMsg2 && (hasMsg1 || hasMsg3)` predicate) / pass-after.
- `pio test -e native`: full suite green.
- `pio run -e cardputer` and `pio run -e m5stick`: both build.

## As built

Shipped with ADR-0145. `CapturedHandshake` carries per-message client MAC + replay counter;
`processMessage1/2/3` populate them; `hasM1M2Pair`/`hasM2M3Pair`/`hasValidPair` gate
`isMinimumValid`/`isWpaSecValid`. Regression in `test/test_handshake_pairing`. Fail-before
confirmed by reverting `isMinimumValid` to the presence-only check (3 of 7 cases fail);
pass-after 7/7. Full native suite and both device builds green.
