---
id: '0144'
title: "PMKID-only UI treatment requires an actual .22000, not just the type label"
type: architecture
status: accepted
date: 2026-11-07
supersedes: []
superseded_by: []
---

## Context

ADR-0141 introduced an `isPmkid` summary flag, derived from the manifest record `type`
(`type == PMKID`), and used it to drive three pieces of Captures UI: the list renders the row
as `{ssid}.22000`, the list status dot follows the pwncrack lifecycle (WPA-SEC being N/A for a
clientless PMKID), and the FILE INFO WPA-SEC row shows "N/A". The reasoning was that a
PMKID-only capture has no `.pcap`, so WPA-SEC does not apply and pwncrack is the one service.

On-device testing (surfaced by the ADR-0142 recap + fail-loud logging, then confirmed by
reading `manifest.bin` off the field card) found this breaks on older records. The card held
**30 records with `type == PMKID` but no PMKID** (`RF_HAS_PMKID` clear): they carry EAPOL
messages (M1+M2[+M4]), have no `.22000` on disk (`RF2_HAS_HC22000` clear), and were already
uploaded to WPA-SEC (`wpaSecStatus == UPLOADED`). They are 4-way/EAPOL captures mislabeled
PMKID by the type-misclassification bug that was fixed for new captures on 2026-06-27; the bad
type persists in these pre-fix manifest records.

For each of them `isPmkid` was true, so the list showed a red `{ssid}.22000` row (dot follows
pwncrack = NOT SENT), while a manual upload reported "Nothing to sync" (no `.22000` for
pwncrack; already uploaded to WPA-SEC). The operator sees an unsyncable red `.22000` that is
actually a finished 4-way already on WPA-SEC. The type label alone is not a safe proxy for
"this capture's artifact is a `.22000`".

## Decision

The PMKID-only UI treatment keys off whether the capture **actually has a `.22000` artifact**,
not the `type` label alone. The predicate becomes `isPmkid && has22000`, encoded once as
`HandshakeSummary::isPmkidOnly()` and used by all three sites: the derived list filename
(`getFilename`), the list status dot, and the FILE INFO WPA-SEC "N/A" row.

A record with `type == PMKID` but no `.22000` on disk (`has22000 == false`) is no longer
treated as PMKID-only: it renders as its `.pcap`, the dot shows the normal best-outcome across
WPA-SEC and pwncrack, and FILE INFO shows its real WPA-SEC status. A true PMKID-only capture
(`type == PMKID` and `has22000 == true`, no `.pcap`) is unchanged.

## Consequences

- The 30 mislabeled records (and any like them) render honestly: a `.pcap` row whose dot
  reflects the real WPA-SEC outcome (orange "uploaded" here), not a misleading red `.22000`.
  "Nothing to sync" is then self-evidently correct rather than a contradiction, because the
  row no longer claims to be a pending pwncrack upload.
- No manifest migration is required. The stale `type == PMKID` label on those records no longer
  has any user-visible effect once the UI gates on `has22000`, so re-typing them (riskier, a
  full-manifest rewrite) buys nothing and is deferred to the ledger.
- Upload and status logic are untouched: this only changes how a row is classified for display.
- This refines ADR-0141 point 3 (it does not supersede it): the `isPmkid` flag still exists and
  still means "record type is PMKID"; the UI simply additionally requires a real `.22000`
  before treating the capture as pwncrack-only.
- A correctly-classified PMKID whose `.22000` is somehow missing (e.g. deleted out from under
  the record) now also renders as a `.pcap` row; that is acceptable, since with no `.22000`
  there is nothing for pwncrack and the row should not advertise one.
