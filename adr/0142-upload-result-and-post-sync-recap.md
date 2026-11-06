---
id: '0142'
title: "Upload services return a result; bulk sync ends on a recap"
type: architecture
status: accepted
date: 2026-11-06
supersedes: []
superseded_by: []
---

## Context

A bulk cloud sync purges the 64800-byte canvas for the whole run (ADR-era memory note in
`captures_screen.cpp`): a TLS handshake needs ~62KB of record buffers that this PSRAM-less
board can only find by freeing the canvas, and the canvas is restored once at the end, not
between files. While it is gone the UI cannot render, so every per-file `showErrorToast` a
service fires during the run paints to nothing. The operator sees only the final
"Sync complete (N)" success toast.

`uploadHandshake` (both WPA-SEC and pwncrack) returns `bool` and reports *why* a file failed
only through those swallowed toasts. The caller at the sync site ignores the `bool` entirely.
So a file that silently fails to upload, for any reason, leaves no trace: no toast the
operator can see, no serial line, and a green "complete" at the end.

This is exactly how the clientless-PMKID "NOT SENT forever" bug (ADR-0141) stayed invisible
for so long: the sync opened a `.22000` path that did not exist, `SD.open` failed, the
function `showErrorToast("No .22000 file")`'d into the purged canvas, returned `false`, and
"Sync complete" printed anyway. The root filename bug is fixed in ADR-0141; the
*observability* hole that hid it is not, and it will hide the next upload failure just as well
(a dropped link, a stale clock, a server rejection).

Two complementary remedies were weighed. A tethered `pio device monitor` during a sync is the
right tool for a developer chasing a specific failure, but it only helps if the failures are
actually logged (today most are toast-only). A persistent on-device recap is the right tool
for the operator in the field with no serial cable. We want both, and they share one
prerequisite: the upload path has to *report* its outcome instead of only toasting it.

## Decision

1. **Upload services return a structured result, not `bool`.** Introduce
   `adversary::UploadResult { UploadOutcome outcome; const char* reason; }` with
   `UploadOutcome ∈ { Sent, AlreadyUploaded, Failed }`, in a small shared header
   (`src/modules/network/upload_result.h`). `WpaSecService::uploadHandshake` and
   `PwncrackService::uploadHandshake` return it. `Sent` means the server newly accepted the
   upload; `AlreadyUploaded` means nothing new was sent (the local skip-check short-circuited,
   or the server reported a duplicate); `Failed` means it did not land, for the reason given.
   The two sync-loop call sites are the only consumers and are updated; a future upload
   service follows the same contract.

2. **Failure reasons are short static labels; dynamic detail goes to the serial log.**
   `reason` points at a string literal (static lifetime), never a stack buffer or an Arduino
   `String`, so the result can be returned and copied without ownership hazards on this chip.
   Anything variable (an HTTP status code, the TLS stream error text, the path that failed to
   open) is written with `LOG_W` / `LOG_E` at the failure site, not packed into `reason`. The
   recap shows the coarse category ("no .22000 file", "connection failed", "no valid
   handshakes"); the serial monitor carries the specifics. This is what makes a tethered sync
   diagnostic: the formerly silent `SD.open` bail and its siblings now log loud.

3. **A bulk sync always ends on a recap modal, not only on failure.** When the run finishes
   (the normal completion branch of `kickNextSync`, after the canvas has been restored), the
   screen raises a modal summarising, per service, how many captures were sent / already done
   / failed, followed by a scrollable list of the failed captures with their service and
   reason. It replaces the terse "Sync complete (N)" toast as the run's end state. A clean run
   shows the happy counts and an empty failure list; a run with failures shows them. The modal
   owns input until dismissed (Enter/ESC). Showing it always (not only on failure) is
   deliberate: the mid-run toasts are gone, so the recap is the one reliable surface, and a
   deliberate multi-file network operation earns one proportionate summary screen. An ESC-
   stopped run still recaps what it managed before stopping.

4. **The recap observes; it does not change upload or status semantics.** A `Failed` or
   server-`INVALID` file is still left for retry / marked INVALID exactly as before. The recap
   only tallies the outcomes the services already decide.

## Consequences

- Bulk-sync failures are no longer swallowed: the operator always sees a truthful end-of-run
  summary, and a tethered `pio device monitor` shows every per-file failure with its path or
  HTTP code. The class of bug that hid ADR-0141 cannot hide the next one.
- `bool` -> `UploadResult` is a breaking signature change on both services. Blast radius is
  two call sites (the WPA-SEC and pwncrack dispatch in the sync loop); both are updated in the
  same change. New upload services return `UploadResult`.
- The result path allocates nothing: `reason` is always a literal, and the recap stores its
  capped failure list in fixed `char[]` buffers (CLAUDE.md: `char[]` over `String` on this
  chip). The modal is built and drawn from the post-restore heap, the cleanest point in the
  run.
- `AlreadyUploaded` vs `Sent` is a real distinction the operator can act on (nothing-to-do vs
  new contribution); server "already in database" counts as `AlreadyUploaded`. Server-`INVALID`
  ("no valid handshakes") is reported under `Failed` with that reason, since it did not upload;
  the gray INVALID dot on the list already carries the "do not retry" nuance.
- The feature itself (the modal component, counts, scroll, the accumulator in the sync loop)
  is built in slice-0143.
