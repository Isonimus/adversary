---
id: '0167'
title: "SD Storage view (read-only overview, carousel entry)"
type: slice
status: accepted
date: 2026-11-26
supersedes: []
superseded_by: []
---

## Goal

Ship the SD Storage view decided by ADR-0166: a new top-level carousel entry **SD**
(placed immediately before MODULES) that opens a read-only one-level overview of
ten curated well-known paths under `/adversary/`, with card capacity at the top and
per-folder `(fileCount, totalBytes)` rows below.

Closes the tincherm31 and Reddit v0.10.0 "where are my handshakes" feedback gap,
and pairs with v0.12.0's dashboard-embed work by giving the operator a one-glance
answer to "did my SD dashboard land?". Deletes the LEDGER.md Distribution/Docs
Storage-view item (line 131) and the roadmap row's `read-only SD "Storage" view`
mention (line 53) as this slice closes them.

## Definition of Done

- **Given** the SD is mounted and `/adversary/` holds data,
- **When** the operator opens the carousel to the SD tile and presses ENTER,
- **Then** StorageScreen renders with a card-capacity panel at the top (`Used /
  Total GB` + percent used) and the ten curated rows, each showing `N files, X
  MB` for its path, with no action available on highlighted rows and ESC
  returning to the carousel.

- **Given** the SD is not mounted,
- **When** the operator opens the carousel,
- **Then** the SD tile is greyed with reason "SD not mounted"; forcing the
  screen anyway (e.g. hot-remove mid-session) renders a single "SD not
  mounted" message and no rows.

- **Given** a curated folder holds more than 500 entries,
- **When** the overview walk runs,
- **Then** the row reports `"500+ files"` plus the first-500 byte sum, and the
  walk returns to the caller without recursing further.

## Design

Per ADR-0166.

New files:

- `src/ui/screens/storage_screen.{h,cpp}` — `StorageScreen : IScreen`. Reuses
  `ScrollableList` + `ListScrollModel` + `FooterHints` + `StatusBar`. ESC-only
  input (no actions). Walks on `show()`, caches the result, re-walks only on the
  next `show()`.
- `src/utils/storage_overview.{h,cpp}` — pure helper. `IDirectoryLister` interface
  + two row structures (`StorageOverview::Row`, const curated `kRows[]`). Returns
  `{ok, fileCount, totalBytes, truncated}` per path. 500-entry hard cap enforced
  here, not in the caller. Native-testable.
- `test/test_storage_overview/test_storage_overview.cpp` — four unit tests: empty
  dir, mixed-files-and-subdirs (files-only counted), >500 entries truncated,
  missing dir returns `ok=false`.
- `src/assets/icons/menu/icon-sd.bmp` — new 48x48x1 BMP, SD-card silhouette,
  line-weight / fill style matching `icon-wireless.bmp` and `icon-server.bmp`.
  Byte-array drop into `src/assets/generated_icons.h` as `ICON_SD`.

Edits:

- `src/ui/menu_factory.cpp` — insert `{"SD", adversary::assets::ICON_SD,
  ACTION_STORAGE, sdMounted, "SD not mounted", []{ return
  SDManager::getInstance().isMounted(); }}` immediately before the MODULES entry.
- `src/ui/screens/screen_manager.cpp` (and `screen_ids.h` if present) — register
  `StorageScreen` behind `ACTION_STORAGE`.
- `LEDGER.md` — delete the Distribution/Docs Storage-view item (line 131).
  Annotate the v0.12.0 roadmap row's `read-only SD "Storage" view` mention with
  `(shipped)` or remove it from the v0.12.0 Main items cell if the whole feature
  is now closed.
- `README.md` — add one bullet under the device UI overview describing the SD
  tile and what it shows.

**What existing code was considered and reused**: `CapturesScreen` was the main
candidate for broadening (it is the existing capture-centric file browser);
rejected for the reasons recorded in ADR-0166. `ScrollableList` and
`ListScrollModel` already drive every list on the device — reused as is.
`FooterHints` and `StatusBar` are standard screen furniture — reused as is. The
carousel entry registration path already handles enabled/reason wiring (same
pattern RFID uses for module-absent); no new mechanism added. `SDManager::isMounted`
already exists and is already used by MODULES; the Storage tile reuses it. The
`.bmp -> ICON_*` encoding in `generated_icons.h` is manually authored (no
generator script in `scripts/`), so the new `ICON_SD` is added by the same hand
pattern as its siblings.

**Deliberately not reused**: `CapturesScreen::drawFileList` and its metadata
machinery. The overview rows are static (curated), not a dynamic directory list;
reusing that path would drag in WPA-SEC status, handshake-type parsing, and the
ActionMenu wiring, all of which are explicitly out of scope per ADR-0166.

**No `any`-equivalent issues**: all types concrete (`uint32_t`, `uint64_t` for
byte totals, `uint16_t` for truncation-capped counts).

**Walk cap rationale**: 500 entries per folder is well above the 2 to 20 entries
these folders carry today (handshakes-captures tops out in the tens on even a
heavy session), gives headroom for an operator who has not pruned in months, and
is a tight enough RAM ceiling that the walk's transient stays flat.

## Verification

- Unit tests: `test/test_storage_overview/test_storage_overview.cpp` covers
  empty, mixed, truncated, missing. Runs under `pio test -e native` as part of
  the suite.
- Device verification (operator-run):
  1. Fresh flash on a Cardputer with an empty SD: open SD tile → all ten rows
     show `0 files, 0 B`, capacity panel shows used/total correctly, ESC exits.
  2. Run one handshake capture: ESC → re-enter SD → Handshakes row shows
     `1 file, X B` (same byte count the file system reports).
  3. Remove the SD during Storage view: no crash, re-entering shows "SD not
     mounted".
  4. Boot without an SD: the SD carousel tile is greyed with reason "SD not
     mounted".
- Build: `pio run -e cardputer` and `pio run -e m5stick` serially (CLAUDE.md
  convention), both green.

## As built

Shipped the read-only SD Storage view as designed in ADR-0166, plus three
refinements from an on-device field pass:

- **Carousel entry** `STORAGE` (labelled so, not "SD", since the icon already
  carries the "SD" glyph) with a new `ICON_SD` (48x48x1 BMP, solid-fill
  silhouette matching the other icons' palette: index 0 = black, index 1 =
  white; an earlier palette had these swapped and rendered inverted), placed
  immediately before MODULES in `menu_factory.cpp`. Greyed with reason "SD not
  mounted" when `SDManager::isReady()` is false; carousel `select()` blocks
  entry on the greyed tile and scroll-nav skips it, same as RADIO/SERVER.
- **`src/utils/storage_overview.{h,cpp}`** — pure `computeStorageRow(IDirectoryLister&,
  path)` returning `{ok, fileCount, totalBytes, truncated}` with a 500-entry
  cap. Four native tests in `test/test_storage_overview/` (empty, files-counted-
  subdirs-skipped, truncation, missing-dir), all green.
- **`StorageScreen`** — card-capacity header (`used / total`, usage bar of our
  own data vs `SD.cardSize()`) plus the ten curated folder rows, each showing
  `N files, X MB`. Reuses `ScrollableList` + `ListScrollModel` + `FooterHints`
  + `StatusBar`.

Field-pass deltas (not in the original DoD, added after testing on a card with
500+ handshakes):

- **Progressive load.** The folder walk was synchronous on `show()` and made
  the UI look frozen while the 500+ file handshakes folder was scanned. It now
  scans one folder per frame behind a "Loading SD card data... / Scanning
  <folder> (n/10)" screen, with a prime flag guaranteeing one Loading frame
  paints before the first (slowest) scan. Intra-folder chunking was considered
  and deferred: the loading message alone removed the frozen-look, and the
  state-machine cost was not justified yet.
- **Selection + inert ENTER.** The list was scroll-only (no visible selection)
  and ENTER bubbled up as an exit to the carousel. Now the list is selectable
  with a `BG_SELECTED` highlight on the current row, and ENTER is swallowed as
  a no-op (read-only overview, no per-row action). Only ESC/backtick exits. The
  per-file date/size drill-in was explicitly declined for this slice (operator
  chose the overview-only path); file-level handshake detail stays with the
  Captures Browser per the ADR boundary.

All three Definition-of-Done scenarios hold: a mounted card renders the
capacity panel + ten rows with no per-row action and ESC back; an unmounted
card greys the tile and the screen shows "SD not mounted"; a 500+ entry folder
reports "500+ files" with the partial byte sum. Native suite green; cardputer +
m5stick builds green; flashed and confirmed on a Cardputer ADV.
