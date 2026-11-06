---
id: '0143'
title: "Post-sync cloud-sync recap modal"
type: slice
status: accepted
date: 2026-11-06
supersedes: []
superseded_by: []
---

## Goal

Give the operator a truthful end-of-run summary of a bulk cloud sync, so a silently failed
upload can no longer hide behind a green "Sync complete". Implements the recap half of
ADR-0142: the upload services already return an `UploadResult`; this slice accumulates those
results across the run and renders them in a dismissible recap modal.

New component `ui::SyncRecapDialog` (header-only, host-testable like `ConfirmDialog`): holds
per-service tallies (sent / already / failed) and a capped list of failed captures, each with
its service and a static reason label. `CapturesScreen` owns one, resets it when a run starts,
appends one outcome per job in the `pendingUpload_` tick, and shows it from the completion
branch of `kickNextSync` once the canvas is restored.

## Definition of Done

- **Given** a bulk sync of several captures to WPA-SEC and/or pwncrack
- **When** the run finishes normally
- **Then** a modal appears (in place of the old "Sync complete (N)" toast) showing, per
  service, the count sent / already-done / failed, and it stays up until the operator presses
  Enter or ESC.

- **Given** some captures failed to upload during the run (no file, bad size, connection
  failed, time not synced, server rejected, no valid handshakes, ...)
- **When** the recap shows
- **Then** each failed capture is listed with its service and a short reason, the list scrolls
  if it exceeds the visible rows, and a count tells the operator how many failed; the matching
  precise detail (HTTP code, path, stream error) is on the serial log for a tethered session.

- **Given** a clean run where every capture was sent or already uploaded
- **When** the recap shows
- **Then** the per-service counts render with an empty failure list and a "no failures" line,
  and a single Enter/ESC returns to the file list.

- **Given** the operator ESC-stops the run partway
- **When** the run halts after the in-flight file
- **Then** the recap still shows what was attempted before the stop, marked as stopped, rather
  than silently discarding the partial tally.

- **Given** more captures failed than the recap's capped failure list can hold
- **When** the recap shows
- **Then** it lists up to the cap and shows a "+N more" line, never overflowing its buffers,
  and the full set is still on the serial log.

## Design

`ui::SyncRecapDialog` (header-only, `src/ui/components/sync_recap_dialog.h`) holds three
counters per service (sent / already / failed) and a capped array of failures, each a copied
SSID + a service tag + a `const char*` reason (a static literal per ADR-0142). `record()` folds
one `UploadResult` in; failures past the cap still count toward the per-service total and show
as a "+N more" overflow line, with the full set on the serial log. A small scroll window
(`;`/`.`) pages the visible failure rows; the dialog is blocking and swallows every other key
so none leaks to the screen. `render()` is a `template<Canvas>` drawn only on the device, after
the canvas is restored.

`CapturesScreen` owns one `syncRecap_`. `startSync` calls `reset()`. The `pendingUpload_` tick
captures each `uploadHandshake` return and calls `record()`. The terminal branches of
`kickNextSync` call `show()`: the normal branch replaces the old "Sync complete (N)" toast, the
ESC-stop branch calls `setStopped(true)` first. The canvas restore in the tick now fires on
`lastJob || syncCancelRequested_`, so an ESC-stop also tears WiFi down and restores the canvas
(previously it did neither, leaving a dead UI) and the recap can draw.

## Verification

- `pio test -e native`: `test_sync_recap_dialog` added (fresh-reset empty, per-service tally
  split, failed-list cap + overflow count, reset clears, scroll clamp both ends, no-scroll when
  the list fits, Enter/ESC/backtick dismiss + callback, stray keys swallowed without closing,
  input ignored when hidden, stopped predicate). Full suite stays green.
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS (built serially).
- On-device (hardware): run a bulk sync with a mix of sendable and un-sendable captures and
  confirm the recap shows the per-service counts and the failed list; tether `pio device
  monitor` and confirm each failure also logs its path / HTTP code.

## As built

As designed.
