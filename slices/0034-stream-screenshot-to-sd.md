---
id: '0034'
title: "Stream the screenshot BMP to SD row-by-row instead of buffering the whole file in RAM"
type: slice
status: accepted
date: 2026-09-22
supersedes: []
superseded_by: []
---

## Goal

Fn+S fails to save from a memory-heavy screen. Field report:

```
[Screenshot] malloc(97254) failed, free heap 92780
```

`saveScreenshot()` allocates the **entire BMP file in one contiguous block** —
`malloc(bmp24FileSize())` at [`screenshot.cpp:60`](../src/utils/screenshot.cpp) — fills it
row-by-row, then hands the whole thing to `SDManager::writeFile()` in a single call. For the
240×135 panel that is `54 + 240·3·135 = 97,254 B`, matching the log. On the no-PSRAM device
(~205 KB internal SRAM is the whole budget; CLAUDE.md "No PSRAM (invariant)") a ~95 KB
**contiguous** request cannot be met once a screen with a full list / packet log / captive portal
is resident — free heap was 92,780 B, below the request, and fragmentation can sink it even when
the nominal free figure looks sufficient. So the feature works from a light screen and dies from a
heavy one — precisely when a screenshot is most wanted.

Nothing requires the whole file in RAM. A BMP is a 54-byte header followed by independent,
bottom-up pixel rows, and the writer already fills it one row at a time. Streaming those rows
straight to the SD file drops the peak transient allocation from ~97 KB to **one row (720 B)**,
~135× less, so the save succeeds regardless of the resident screen. This is the root-cause fix, not
a workaround: it makes the No-PSRAM invariant hold for this feature.

### Scope: stream via a held file handle, reusing the PcapWriter pattern

The existing SD-streaming precedent in the tree is `PcapWriter`: it holds an `fs::File` open
(`SD.open(FILE_WRITE)` once → `m_file.write()` per chunk → `close()`) rather than re-opening per
write. `screenshot.cpp` is already device-only (`#if defined(TARGET_CARDPUTER) ||
defined(TARGET_M5STICK)`) and already talks to the SD layer, so it adopts the same pattern: open the
file once, write the header, write each row, close once.

Rejected alternatives:

- **Loop `SDManager::appendFile()` per row** — `appendFile()` does `open(APPEND) → write → close`
  every call ([`sd_manager.cpp:378`](../src/hal/storage/sd_manager.cpp)), so 135 rows = 135
  open/close cycles: needless FS churn and SD wear for no benefit.
- **Add a general streaming-writer API to `SDManager`** — only the screenshot writer needs it
  (PcapWriter already holds its own handle), so a shared abstraction has no second consumer and is
  speculative generality (rule 3). Revisit only if a third streaming writer appears.

## Definition of Done

- **Given** a screen resident that leaves free heap below the full BMP file size (~97 KB) but above
  one padded row (~720 B) — the exact condition that produced the `malloc(97254) failed` report
- **When** the operator presses Fn+S
- **Then** the screenshot is written successfully (no OOM), because the peak transient allocation
  during the save is one row, not the whole file

- **Given** a saved screenshot
- **When** it is opened on a host
- **Then** it is a valid 24-bit BMP, right-side-up and correct-coloured — i.e. the streamed
  bottom-up row order and the B,G,R byte order are preserved exactly as before this change

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** a new assertion set covers the pure file-row → source-canvas-row mapping the streaming
  loop depends on (`bmp24SourceRow`): endpoints invert (`fileRow 0 → height-1`,
  `fileRow height-1 → 0`) and the map is a bijection over `[0, height)` — failing to compile before
  this slice (the function does not exist) and green after. This is the off-by-one that would
  silently save the image upside-down — the same class of layout bug `bmp_header.h` was split out to
  guard (see its file header).

- **Given** a mid-stream SD write failure (short write or error on any row)
- **When** the writer detects it
- **Then** it closes the handle, deletes the partial file (never leaves a truncated, blank-rendering
  BMP), and returns failure with a logged reason — the fail-loud contract the current writer already
  honours for the single-write path

## Design

### The tested piece: file-row → source-row mapping

Streaming sequentially means emitting rows in on-disk order (bottom-up), so **file row `r` reads
source canvas row `height-1-r`**. Today that arithmetic is inline at `screenshot.cpp:83`
(`height - 1 - y`, used as a random-access *destination* offset into the buffer); streaming turns it
into the *source* index of a forward loop, which is exactly where the vertical flip inverts. Extract
it as a pure `constexpr` helper in `bmp_header.h`, beside the other layout math, and unit-test it:

```
// File (on-disk) rows are bottom-up: file row 0 is the image's bottom row. Given a top-down
// source canvas, file row r comes from source row (height-1-r). Guards the vertical-flip off-by-one.
constexpr uint16_t bmp24SourceRow(uint16_t fileRow, uint16_t height) {
    return static_cast<uint16_t>(height - 1u - fileRow);
}
```

This keeps the native-testable layout logic in `bmp_header.h` (its stated purpose) and leaves only
the device-bound `readRect`/`File` I/O untested-by-nature, confirmed on-device.

### The streaming writer

In `saveScreenshot()`, after the unchanged SDManager setup (`isReady()`, dir create, free-slot
`shot_NNN.bmp` scan) — replace the single `malloc(fileSize)` + `writeFile()` with a held handle:

1. `fs::File file = SD.open(path, FILE_WRITE);` — fail loud if it does not open. (Direct `SD.open`,
   as `PcapWriter` does; the SDManager setup calls above still gate on `isReady()`.)
2. Build the 54-byte header on the stack (`uint8_t header[bmp::BMP24_HEADER_SIZE]`;
   `writeBmp24Header(header, w, h)`) and `file.write(header, 54)`.
3. Allocate **one** row buffer: `malloc(rowBytes)` (~720 B — trivially satisfiable; no fixed-max
   width assumption). Zero it once so any 4-byte row padding tail stays 0 (readRect fills only
   `width·3`; current targets are 240 px = 720 B, unpadded, but the zero keeps padded widths correct).
4. For `r` in `[0, height)`: `readRect(0, bmp24SourceRow(r, h), width, 1,
   reinterpret_cast<lgfx::rgb888_t*>(rowBuf))` then `file.write(rowBuf, rowBytes)`. The
   `rgb888_t`-vs-`bgr888_t` byte-order rationale (LovyanGFX `rgb888_t` is `{b,g,r}` in memory =
   BMP order) carries over verbatim — keep the comment.
5. Verify every `write()` returns the expected count; on any short write, `close()`, `free(rowBuf)`,
   `SD.deleteFile(path)` via SDManager, log, and return false. On success, `close()`, `free(rowBuf)`,
   fill `outPath`, log the saved path + byte count as today.

Peak transient allocation during a save: `rowBytes` (~720 B) + the file handle, versus `fileSize`
(~97 KB) before. The header buffer is 54 B on the stack.

### Boyscout

The `memset` of the whole pixel area for padded widths (`screenshot.cpp:73`) collapses to a single
zero of the ~720 B row buffer — smaller and clearer. No other behaviour changes: same filename
scheme, same header bytes, same pixel order, same fail-loud-and-delete-partial contract.

## Verification

- Native: `pio test -e native` — full suite green, including new `bmp24SourceRow` assertions
  (endpoints invert + bijection over `[0, height)`); fails to compile before this slice (the
  function does not exist).
- Build: `pio run -e cardputer` — compiles and links; report RAM/Flash vs the slice-0032 baseline
  (Flash 2,466,531 B). Flash should barely move; static RAM is unchanged (the ~97 KB was heap, not
  a global).
- Heap (the point): the transient allocation during a save is one row (~720 B), not the file
  (~97 KB). Not unit-testable on native (device heap); confirmed on-device below.
- On-device (deferred to the LEDGER, as with slice-0026/0028/0029/0030/0031): open a heavy screen
  (Scanner or Sniffer with a full list/log — the state that logged `malloc(97254) failed`), press
  Fn+S, confirm it now saves with no OOM; pull the BMP, run it through
  `scripts/screenshots-to-png.sh`, and eyeball that it is right-side-up with correct colours
  (red/blue not swapped). Repeat from a light screen (menu) to confirm no regression.

## As-built

Shipped as designed (Option A). `bmp_header.h` gained the pure `constexpr bmp24SourceRow(fileRow,
height) = height-1-fileRow`, tested natively. `screenshot.cpp` replaced the single
`malloc(fileSize)` + `SDManager::writeFile()` with a held `fs::File` (`SD.open(path, FILE_WRITE)`,
matching `PcapWriter`): the 54-byte header is built on the stack and written, then one ~720 B row
buffer is `malloc`ed once, zeroed for padding, and reused across the `for r in [0,height)` loop —
`readRect(0, bmp24SourceRow(r, height), …)` into it, `file.write(row, rowBytes)` out. Peak transient
allocation during a save is one row (~720 B) instead of the whole ~97 KB file. A local `abortWrite`
lambda closes the handle, `SD.remove()`s the partial file, and logs on any failed open/header/row
write — preserving the never-leave-a-truncated-file contract. `<SD.h>` is now included directly (as
`PcapWriter` does) rather than relied on transitively. The `rgb888_t`-vs-`bgr888_t` byte-order
comment is retained verbatim.

Verification:
- Native: `pio test -e native` **773/773** (was 772; +1 = `test_source_row_is_bottom_up_bijection`
  in `test/test_bmp_header/` — endpoints invert + bijection over `[0,H)`; fails to compile before
  this slice as `bmp24SourceRow` did not exist).
- Build: `pio run -e cardputer` **SUCCESS**. Flash 73.8% (**2,466,867 B**, **+336 B** vs the 0032
  baseline of 2,466,531 B — the loop/lambda/`<SD.h>`; negligible). Static RAM unchanged: the ~97 KB
  eliminated was a heap allocation, not a global.
- Doc-kit: `lint-docs` ok; `adr/INDEX.md` regenerated.
- On-device: **confirmed 2026-09-22** on the Cardputer. Fn+S now saves from heavy screens (Scanner
  and Sniffer with full lists — the state that logged `malloc(97254) failed`); ~18 captures were
  written across all major screens with no OOM, each opening right-side-up with correct colours
  (red/blue not swapped), verified after conversion via `scripts/screenshots-to-png.sh`. The
  bottom-up row order and B,G,R byte order are intact.
