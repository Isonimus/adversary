---
id: '0166'
title: "SD Storage view (read-only overview, carousel entry)"
type: architecture
status: accepted
date: 2026-11-26
supersedes: []
superseded_by: []
---

## Context

External testers (tincherm31; the Reddit v0.10.0 thread) report the same gap: once a
capture is made, they cannot find where it lives on the SD card without a laptop. The
firmware knows where everything is because the modules themselves write to fixed
subpaths under `/adversary/` (`captures/handshakes/`, `captures/credentials/`,
`captures/packets/`, `subghz/`, `ir/`, `badusb/`, `dashboard/`, `logs/`, `coredumps/`),
but the operator never sees that layout surfaced on the device itself. The adjacent
"dashboard is blank" gap has the same root cause: the operator does not know whether
`dashboard/` has content on the card or not.

Captures Browser (`src/ui/screens/captures_screen.cpp`) exists and does solve part of
this, but it is capture-centric by design: it only knows the four `/adversary/captures/`
subcategories (Packets, Handshakes, Credentials, RFID, Wardriving), parses .22000
metadata, drives WPA-SEC and pwncrack sync, and deletes files behind the ADR-0085
confirm overlay. It is the write/manage surface for the capture tree and deliberately
blind to the rest of the card (BadUSB scripts, IR codes, SubGHz captures, dashboard
assets, logs, coredumps). Extending it to cover the whole tree would blur its purpose
and compete with the per-type management screens (Saved Networks, IR Record, SubGHz
captures) that each already own their own content with their own actions.

The ledger entry for this view (LEDGER.md line 131, 2026-10-25) already settled the
scope question: keep it narrow and **read-only**, confirm-a-capture-landed rather than
file-manager, so the ADR-0085 destructive-verb consistency and the per-type management
screens stay unchallenged.

## Decision

Add a new top-level carousel entry **SD** (`ICON_SD`, new 48x48x1 BMP) placed
immediately before MODULES, opening a new read-only screen `StorageScreen`
(`src/ui/screens/storage_screen.{h,cpp}`).

The screen is a one-level overview: a header panel with card capacity (`Used / Total`
in GB plus % used), and a scrollable list of **curated well-known paths** showing the
file count and the total byte size of each. No file-level drill-in, no actions on
rows, no deletion, no cross-navigation into the per-type management screens. ESC
returns to the carousel.

Curated rows (fixed order, 10 total):

| Label         | Path                               | Rationale |
|---|---|---|
| Handshakes    | `/adversary/captures/handshakes/`  | Headline "where are my handshakes" gap |
| Credentials   | `/adversary/captures/credentials/` | Evil Twin / Karma output |
| Packets       | `/adversary/captures/packets/`     | Sniffer PCAPs |
| Wardriving    | `/adversary/captures/wardriving/`  | WiGLE CSVs |
| SubGHz        | `/adversary/subghz/`               | Radio captures |
| IR codes      | `/adversary/ir/`                   | IR Record output |
| BadUSB        | `/adversary/badusb/`               | "did my script land?" |
| Dashboard     | `/adversary/dashboard/`            | Dashboard-blank diagnosis |
| Logs          | `/adversary/logs/`                 | Error log triage |
| Coredumps     | `/adversary/coredumps/`            | ADR-0152 crash rescue |

Deliberately **not** in the overview: `config/`, `cache/`, `portals/`, `whitelist/`,
`settings/`, `screenshots/`. These are internal, operator-invisible, and surfacing
them would add noise without closing a known gap.

Walk cost model:

- `computeOverviewRow(path)` lists one directory one level deep (no recursion into
  subdirs), returning `{fileCount, totalBytes}`. One pass per row, computed once on
  `show()`, cached for the lifetime of the screen. ESC + re-enter triggers a fresh
  walk. This matches how the operator uses the screen: a snapshot taken when they
  open it, not a live view.
- Hard cap at 500 entries per folder: beyond that, the row reports `"500+ files"` and
  the partial byte sum. Guards against a pathological card (never observed in the
  field but trivially possible if an operator sideloads).
- Per-row walk is the source of per-row totals; card totals come from `SD.cardSize()`
  (which is cheap and reliable on arduino-esp32). `SD.usedBytes()` is not used (its
  implementation on arduino-esp32 is a full card scan — expensive and the delta from
  our known-folder sum is not something the overview should visualize anyway).
- Screen refuses to walk when `SDManager::getInstance().isMounted() == false`:
  renders a single "SD not mounted" message and no rows. The carousel tile is kept
  visible but greyed with that same reason.

File-on-select behaviour (deliberate non-decision for this ADR): there is no file
selection. The highlight is at row (folder) granularity. Drill-in and file
inspection are explicitly deferred; the ledger item named them as the overlap
surface with the per-type screens and this ADR adopts that boundary.

Native-testability: the walk helper lives in `src/utils/storage_overview.{h,cpp}` as
a pure `StorageOverview::computeRow(IDirectoryLister&, const char* path)` returning
`StorageOverview::Row{ok, fileCount, totalBytes, truncated}`. `IDirectoryLister` is
a tiny interface already matched by the existing SD mock pattern in `test/mocks/`;
the host test rig stubs it with a fixed tree. This keeps every row-count and
truncation semantic covered without the SD hardware.

Components reused: `ScrollableList` + `ListScrollModel` for the overview rows,
`FooterHints` for ESC + scroll hints, `StatusBar` (as every screen). No `ActionMenu`,
no `ConfirmDialog` (read-only by construction).

Carousel entry conditions: shown always; the tile is enabled iff SD is mounted. When
greyed, the hover reason is `"SD not mounted"`. Rationale for always-visible: the
tile exists partly to teach the operator that the device has an SD-first design;
hiding the tile on no-card would contradict that signal and add an inconsistency with
how MODULES handles missing hardware (always visible, greyed on absent).

Placement: immediately before MODULES in `menu_factory.cpp`. Statistically more
operators will use the SD than external modules, so SD precedes MODULES in the
reading order.

## Alternatives considered

**Extend `CapturesScreen` to cover the whole `/adversary/` tree.** Rejected: that
screen is the capture-centric write/manage surface, with ADR-0085 delete semantics,
WPA-SEC/pwncrack sync, and per-type parsing. Broadening it would compete with the
dedicated per-type management screens (Saved Networks, IR Record, SubGHz captures),
fragment the destructive-verb consistency, and turn the simple "where is my data"
question into a navigation puzzle inside a screen whose list is already dense with
sync-status badges.

**Hang Storage off the Modules screen.** Rejected because the ledger entry explicitly
calls for a carousel entry, and the operator feedback is a discoverability problem:
burying it inside Modules recreates the "nested menu" gap the view exists to close.

**Full recursive tree walk.** Rejected: the overview answers "where does my data
live and how much is there", and a one-level-per-row sum gives that answer with no
depth-unbounded RAM cost. Recursion would only matter if a subcategory grew its own
subtree, which none of the ten curated paths does today.

**`SD.usedBytes()` for the card panel.** Rejected: on arduino-esp32 it performs a
full card scan to compute the number, which blocks the UI for seconds on a large
card. The curated per-row sum answers the useful question (how much of my data is
where) more cheaply than the full used-byte number would.

**Writable Storage view (delete, rename).** Rejected per LEDGER.md line 131: the
per-type management screens own write semantics for their own content; a second
generic delete path would fragment the ADR-0085 destructive-verb consistency and
spend SRAM on browsing rather than operator value.

## Consequences

- One new carousel entry, one new screen, one new helper file, one new unit-test
  file, one new icon, one `menu_factory.cpp` registration diff. No changes to any
  existing capture/management screen.
- Captures Browser remains the single owner of `/adversary/captures/*` write and
  manage actions. The two surfaces overlap only at the "handshakes folder has N
  files" level, which is exactly the overlap the overview is for.
- Dashboard-blank diagnosis becomes a one-glance check: the operator sees whether
  `/adversary/dashboard/` has files on the card or not, which pairs with the
  `board_build.embed_files` work landing in the same release (SD overlay wins when
  present, so the operator's first instinct — "did my SD dashboard land?" — is
  answerable from the device).
- Hard SRAM cost: the overview holds 10 rows of (label, path, counts). Negligible.
  The walk's peak transient is a single `File` iterator plus the running sum.
- No new third-party deps, no new events, no changes to the event bus.
- Drill-in and per-file inspection stay as future work; the ADR records that as a
  deliberate boundary and not an oversight.

## Verification

- Unit tests over `StorageOverview::computeRow` against a stubbed
  `IDirectoryLister` cover: empty dir returns `{0, 0}`; dir with mixed files + dirs
  returns correct `fileCount` (files only) + `totalBytes`; dir with >500 entries
  returns `truncated=true` and the first-500 sum; missing dir returns `ok=false`.
- On-device integration: a fresh install shows all ten rows with zero or small
  counts; after a capture run, the Handshakes row updates on next `show()`; with
  the SD removed the screen renders "SD not mounted" and the carousel tile is
  greyed with the same reason.
