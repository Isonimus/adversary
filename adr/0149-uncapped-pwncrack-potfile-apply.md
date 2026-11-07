---
id: '0149'
title: "Match-then-apply pwncrack potfile results without a fixed cap, over a verified parser"
type: architecture
status: accepted
date: 2026-11-07
supersedes: []
superseded_by: []
---

## Context

`PwncrackService::fetchCrackedResults` streams the `download_potfile_script` response line by
line, parses each cracked row, and records it into a fixed `crackedBuffer[32]`. Only after
`http.end()` does it apply the buffered rows to the manifest. Two defects live in that path.

**1. The 32-entry cap silently drops recovered passwords.** Every parsed row increments
`downloadedCount`, but a row is stored for applying only while `crackedBufferCount < 32`. A
potfile with more than 32 crackable rows parses all of them and applies at most 32, with no
warning. The dropped rows are real recovered passwords for the operator's own captures: they are
lost until some later fetch happens to carry them inside the first 32, which it may never do. The
order of potfile rows is not correlated with which captures are on the device, so on a device with
dozens of captures a match can sit past row 32 and never be applied.

**2. The SSID fallback matched the wrong potfile field.** The parse took the text before the first
colon (field index 0) as the SSID used for the fallback match. pwncrack's potfile layout, verified
firsthand by the sibling project esp32-wifi-sapper against the live site and the community
`pwncrack-addons` plugin (its `ADR-0109` decision #5, `pwncrack_potfile_parser`), is: **BSSID at
field index 1, ESSID at field index 3, password after the fourth colon**, and **field 0 is an
auxiliary name field that is ignored**. So the primary BSSID match (field 1) was correct, but
whenever the BSSID was missing or unmatched the fallback compared the operator's capture SSIDs
against an auxiliary field, not the ESSID, and quietly failed to match captures it should have
recovered.

Both defects were untestable where they lived: the parse and the cap were trapped inside an
`#ifdef ESP32` lambda in a network-bound function, so no native test could reach them.

Why the apply is deferred past `http.end()` at all: this is the no-PSRAM board (CLAUDE.md "No
PSRAM" invariant), the TLS session holds roughly 62 KB while the socket is open, and the heap here
is fragmentation-sensitive (see the Karma-TLS fragmentation finding). Doing manifest SD writes
while that TLS allocation is live is the thing the deferral deliberately avoids. Any fix must keep
that discipline: no SD work while the socket is open.

## Decision

Rework the potfile path into a pure, host-tested parser plus a **match-then-apply** flow, keeping
all SD work after `http.end()`.

1. **Verified pure parser.** Add `parsePotfileLine(line, len, PotfileResult&)` in
   `src/modules/network/pwncrack_potfile.*`. It extracts BSSID from field 1, ESSID from field 3,
   and the password verbatim from everything after the fourth colon (so a password containing `:`
   survives), matching the sibling's verified layout. A row is a result only when it has at least
   four colons and a non-empty password. The BSSID field is tolerant (bare or hyphen-grouped
   12-hex); if it does not parse, `bssidValid` is false and the row still stands for an SSID-only
   match, which preserves this firmware's lenient "match by BSSID or SSID" behaviour (unlike the
   sibling, which keys strictly on BSSID). This corrects defect 2: the SSID now comes from field 3.

2. **No cap: match during the drain, apply after.** A header-only `collectPotfileResults` drives a
   line source through `parsePotfileLine` and invokes a sink for **every** valid row, with no
   fixed limit. On the device the sink matches each row against the already-resident `summaries_`
   (a pure RAM scan, no SD, no allocation) and buffers only the **matched** rows, de-duplicated by
   capture. That buffer is bounded by the number of distinct local captures (`summaries_.size()`),
   which is the real ceiling: the device can never apply more cracks than it has captures, and an
   unmatched row was discarded before anyway. After `http.end()` the buffered matches are written
   to the manifest. This removes the arbitrary 32 cap (defect 1) while keeping zero SD work during
   the live TLS session and RAM bounded by data already in memory.

3. **Split match from write in the registry.** `CaptureRegistry::setPwncrackCrackedPassword`
   (find-then-write in one call) is replaced by `findSummaryIndexByBssidOrSsid` (pure RAM match,
   returns an index) and `applyCrackedAt(index, bssid, password)` (the manifest write). The fetch
   calls the first during the drain and the second after the socket closes. The summary indices
   stay valid because nothing mutates `summaries_` between the two phases (the fetch is
   single-threaded for the duration).

4. **Host-tested at the real boundary.** `collectPotfileResults` and `parsePotfileLine` are the
   exact units the device runs, and both are pure, so `test/test_pwncrack_potfile` drives a potfile
   of more than 32 valid rows through `collectPotfileResults` and asserts every row reaches the
   sink (the cap regression), and pins the field layout (BSSID 1, ESSID 3, password-with-colons
   after colon 4) against the verified fixtures. A capped collector fails the first; a field-0 SSID
   fails the second.

This stays a sibling parser, not a shared one with wpa-sec: wpa-sec is BSSID 0 / ESSID 2 /
password after the third colon, pwncrack is BSSID 1 / ESSID 3 / password after the fourth. At two
backends the rule of three keeps each parse a flat, independently tested function rather than a
branchy shared one, the same call the sibling's `ADR-0109` made.

## Consequences

- A fetch applies every cracked result that matches a local capture, not just the first 32. A
  large potfile no longer strands recoveries, and there is no silent cap to tune.
- Captures that only matched by SSID (BSSID missing or renamed) are now recovered, because the
  fallback compares the real ESSID (field 3) instead of the ignored field 0.
- No SD write happens while the TLS socket is open, so the no-PSRAM fragmentation discipline the
  original deferral protected is preserved. Peak RAM for the match buffer is bounded by the local
  capture count, which is already resident in `summaries_`.
- The potfile parse is unit-tested on the native lane with no device, its fixtures frozen on the
  verified field layout. A future pwncrack format change surfaces as failing parse tests rather
  than a silent parse-to-nothing.
- If pwncrack ever changes the potfile layout, only `parsePotfileLine` and its fixtures move, and a
  fresh probe is the evidence, the same contract-is-measured discipline as ADR-0148.
