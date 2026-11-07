---
id: '0141'
title: "Canonical .22000 path and SSID-keyed upload status"
type: architecture
status: accepted
date: 2026-11-05
supersedes: []
superseded_by: []
---

## Context

The clientless-PMKID work (ADR-0137/0138) added a second producer of the hashcat
`.22000` artifact, `handshake_save::savePMKIDToFile`. It named the file
`{sanitize(ssid)}_{BSSID3}.22000`. But the pre-existing 4-way producer,
`writeHandshake22000`, names it `{raw-ssid}.22000`, and the Captures explorer's
pwncrack sync reconstructs the path a third way, `{ssid}.22000` from the summary's
display SSID. The three disagreed on the one filename they all have to agree on.

Consequence, observed in the lab: a PMKID-only capture registers with
`has22000 = true` and `pwncrackStatus = NOT_UPLOADED`, so the sync *does* queue it,
builds `/adversary/captures/handshakes/{ssid}.22000`, finds no file there (the real
file carries the `_{BSSID}` suffix), fails `SD.open` with "No .22000 file", and
leaves the status at NOT_UPLOADED. Every clientless PMKID was stuck at pwncrack
"NOT SENT" permanently and was never actually uploaded.

Two adjacent facts surfaced while tracing it:

- The save paths sanitize the SSID for the filename (`filename_utils`), but the sync
  reconstructs the path from the raw display SSID, and both upload services key the
  registry status off the filename stem. For an SSID containing FAT32-illegal
  characters (`/ : * ?` ...) these diverge, so even 4-way/WPA-SEC uploads can miss the
  file and fail to record status. Pre-existing, broader than PMKID, tracked in the
  ledger as the SSID-sanitization gap.
- A PMKID-only capture shows WPA-SEC status INCOMPLETE. That is correct: WPA-SEC
  ingests a `.pcap` (beacon + M1 + M2) that a clientless PMKID never produces. But it
  renders as a white "INCOMPLETE", which reads to the operator as a broken capture
  rather than "this service does not apply".

## Decision

1. **One canonical `.22000` path.** Add `filename_utils::getHc22000Path` /
   `getHc22000Filename`, mirroring `getHandshakePath`: sanitized SSID, no per-record
   suffix, `{SD_HANDSHAKES_PATH}/{sanitize(ssid)}.22000`. The 4-way save, the PMKID
   save, and the Captures sync all build the `.22000` path (and the sync builds the
   `.pcap` path) through `filename_utils`; no code hand-builds a handshake artifact
   path or hardcodes the captures directory. The `_{BSSID}` suffix on the PMKID file
   is removed: the capture store is SSID-file-keyed like every other artifact. The
   same-SSID / different-BSSID file collision that implies is the pre-existing,
   accepted store limitation (the 4-way `.pcap`/`.22000` already collide that way) and
   is not re-litigated here.

2. **Upload status is keyed by the capture SSID, passed explicitly.**
   `WpaSecService::uploadHandshake` and `PwncrackService::uploadHandshake` take the
   SSID as a parameter and use it for both the already-uploaded skip-check and the
   status write, instead of re-deriving it from the filename stem. The sync already
   holds the SSID (`summary.ssid`) and passes it. This keeps the manifest status
   correct once filenames are sanitized, including for SSIDs whose sanitized stem
   differs from the display SSID.

3. **A PMKID-only capture shows "N/A" for WPA-SEC, not "INCOMPLETE".** The Captures
   FILE INFO WPA-SEC row renders a neutral "N/A" when the record type is PMKID, and
   the list status dot for a PMKID-only entry follows the pwncrack lifecycle (its only
   applicable service) rather than WPA-SEC's INCOMPLETE. For a 4-way entry (which owns
   both a `.pcap` and a `.22000`) the dot shows the best outcome across both services:
   cracked beats uploaded beats not-sent, so a pwncrack-only crack is never hidden by
   WPA-SEC's view. A record-derived `isPmkid` summary flag carries the capture type to
   the list rows, populated where `has22000` and `hasGPS` are populated.

4. **The explorer treats the `.22000` as a first-class artifact.** The captures list
   derives a PMKID-only row's name as `{sanitize(ssid)}.22000` (there is no `.pcap` to
   show), and delete recovers the SSID stem by stripping at the last `.` rather than a
   fixed `.pcap`, so a `.22000` row deletes its registry entry and file instead of
   silently no-op'ing. Delete then removes every artifact sharing the stem (`.pcap`,
   `.22000`, `.json`), so removing a 4-way entry no longer orphans its `.22000`.

## Consequences

- Every clientless PMKID becomes genuinely pwncrack-uploadable; the lab
  "NOT SENT forever" behavior is fixed at the root (the filename the three sites
  share), not patched at one call site.
- The raw-vs-sanitized divergence is closed for the handshake *upload* path (save +
  sync path build + status write), so captures with FAT32-illegal SSID characters now
  upload and record status correctly. The remaining filename-stem-derived *read*
  lookups (`getStatus` / `getCrackedPassword` used by the list and detail views) stay
  as they are: clean-SSID-correct, with the residual special-char display case left
  under the existing SSID-sanitization ledger item rather than widened into this change.
- PMKID rows no longer read as defective captures: WPA-SEC shows N/A, and the row dot
  goes orange/green as pwncrack uploads and cracks.
- PMKID rows list and delete correctly: the row shows `{ssid}.22000` (not a phantom
  `.pcap`), deleting it removes the entry and the real file, and deleting any 4-way
  entry cleans up its `.22000` companion instead of leaving it to re-queue forever.
- `filename_utils` gains byte-exact tests that the `.22000` path is sanitized,
  suffix-free, and shares the `.pcap` stem: the invariant the three producers now
  depend on.
