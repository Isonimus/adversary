---
id: '0173'
title: "Reconcile handshake quality to one crackability-based helper"
type: slice
status: accepted
date: 2026-11-27
supersedes: []
superseded_by: []
---

## Goal

Replace the two colliding handshake-quality scales on the same concept with one
pure, crackability-based helper used by both save and list render. Today
`src/utils/handshake_save.h:270` persists `metadata.quality = msgCount * 25`
(M1-only reads 25, full 4-way reads 100), while `src/ui/screens/handshake_screen.cpp:2099`
ignores the stored byte and recomputes PMKID=100, M1+M2+M3=90 (regardless of M4),
M2-present=70, else=50 at render time. Both are substantively wrong on
crackability (lone M1 over-credited, lone M2 over-credited, complete 4-way capped
at 90 in the list). Fix together: a shared `computeHandshakeQuality` helper
returning a cracking-oriented tier, consumed by the save path and the list
render so stored == displayed by construction. Closes the LEDGER entry
"Reconcile handshake quality scale: one crackability-based helper, called by
both save and list render." (deleted in this slice).

## Definition of Done

- **Given** a captured 4-way handshake with M1+M2 whose client MAC and replay
  counter match (crackable),
- **When** the save path persists metadata and the list screen renders the row,
- **Then** both the persisted `metadata.quality` and the list-rendered percent
  read 100, from the same helper.

- **Given** a clientless PMKID capture (no 4-way),
- **When** the PMKID-only save path persists metadata and the list screen
  renders the row,
- **Then** both read 100 from the same helper.

- **Given** a stray lone M1 (ANonce only, no MIC, not crackable),
- **When** the save path persists metadata and the list screen renders the row,
- **Then** both read 25 (fragment), down from the old `msgCount * 25` = 25 on
  save but 50 on list.

- **Given** a capture with M1+M3 only (two messages, no MIC),
- **When** both consumers run,
- **Then** both read 50 (partial, not crackable), up from the old 50 on save
  but which the old list also showed as 50 only coincidentally.

## Design

New pure helper lives in `src/utils/handshake_quality.{h,cpp}` with a trivial
input struct `HandshakeQualityFlags { bool hasMsg1, hasMsg2, hasMsg3, hasMsg4,
hasPMKID; }` and a free function `int computeHandshakeQuality(const
HandshakeQualityFlags&)`. The helper does not take `CapturedHandshake` or
`SessionHandshakeEntry` directly so both consumers can call it without
pulling one another's header (the list-screen list uses `SessionHandshakeEntry`,
a ~45-byte summary; the save path has the full `CapturedHandshake` + a
separately-known `capture.hasPMKID()` boolean).

Tier logic (crackability-based, LEDGER spec frozen 2026-11-23):

- **100** = `hasPMKID` OR (`hasMsg2 && (hasMsg1 || hasMsg3)`) — PMKID is
  crackable on its own; the ANonce + MIC pair is the hashcat-crackable
  minimum.
- **25** = exactly one of M1/M2/M3/M4 present and no PMKID — fragment.
- **50** = any other non-empty combination that doesn't qualify for 100 —
  partial but not crackable (e.g. M1+M3 without M2, lone M2 without ANonce).
- **0** = no messages and no PMKID.

Pair-validity (ADR-0145 same-client/same-replay) is deliberately NOT a helper
input: the save path already gates writes on `hasValidPair()` so an invalid
set never reaches metadata, and the list-screen `SessionHandshakeEntry`
doesn't carry replay counters. The helper takes presence only.

**Existing code considered.** Searched `src/utils/` for a prior quality or
crackability helper: none (only `handshake_utils.h` type-string classifier,
`hc22000_format.h`, `handshake_save.h` for the save path, `handshake_utils.h`
for the type classifier). The classifier was deliberately not reused:
"type string" and "quality tier" answer different questions and would couple
the badge text to the percent, blocking independent evolution. The two call
sites (`handshake_save.h:270`, `handshake_save.h:400`, `handshake_screen.cpp:2099`)
are the first and second instance of the quality concept respectively —
extracting on the second occurrence is justified here (not speculative) because
the LEDGER entry documents the drift already caused by two independent
implementations.

No migration. Old captures recompute from the already-persisted `hasMsg1..4`
+ `hasPMKID` manifest fields; the stored `quality` byte is overwritten on
next save-path write but never read back for display (the list screen always
recomputes).

## Verification

Native unit test `test/test_handshake_quality/test_handshake_quality.cpp`
asserts each tier:
- PMKID alone -> 100
- M1+M2 (ANonce+MIC) -> 100
- M2+M3 (M3's ANonce + M2's MIC) -> 100
- Full 4-way -> 100
- Lone M1 -> 25
- Lone M2 (no ANonce) -> 50
- Lone M3 -> 25
- M1+M3 (no MIC) -> 50
- Nothing -> 0
- PMKID + any message combination -> 100 (PMKID dominates)

Both device builds (`pio run -e cardputer -j 2`, `pio run -e m5stick -j 2`)
stay warning-clean (ADR-0083) after the call-site rewires. Full native suite
remains green.

## As built

Shipped as specified. `src/utils/handshake_quality.{h,cpp}` carries the
single tier function with a `HandshakeQualityFlags{m1,m2,m3,m4,pmkid}` input
so both consumers call it without pulling one another's heavy header. Save
path at `src/utils/handshake_save.h` (4-way and PMKID-only writes) and list
render at `src/ui/screens/handshake_screen.cpp` now derive quality from the
same helper, so a captured value in the manifest equals the rendered percent
by construction. 12 regression tests in `test/test_handshake_quality/` cover
each tier and the PMKID-dominance rule. All four DoD scenarios held:
full 4-way reads 100 (was 90 in list), PMKID reads 100, lone M1 reads 25
(fragment), M1+M3 reads 50 (partial). No migration: old manifest `quality`
bytes are overwritten on next save and never read for display. Both device
builds (cardputer 77.5%, m5stick 81.0%) stay warning-clean; full native suite
1214/1214 green.
