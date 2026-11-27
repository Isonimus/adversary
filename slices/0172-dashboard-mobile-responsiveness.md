---
id: '0172'
title: "Dashboard mobile responsiveness"
type: slice
status: accepted
date: 2026-11-27
supersedes: []
superseded_by: []
---

## Goal

The embedded dashboard (slice-0169) renders cleanly on a laptop but overflows the
viewport width on phones below ~400 px. The handshake list is the most visible case
(observed 2026-11-26 on a phone against the just-embedded dashboard): the four-column
table punches past the viewport edge and the operator has to pinch-zoom-pan to read
status or GPS. Same shape on captures and wardriving lists, same root cause: hand-rolled
CSS with no small-viewport rules on the tables, the card padding, or the grid min column.

Decision made first (ledger, line 212): **quick CSS-only patch now, do not block on the
Cinder dashboard rewrite** (REST API + Cinder epic, feature backlog). The rewrite lives
behind a REST-API design pass that is not scoped for v0.12.0; the operator has the
embedded dashboard on their phone today. A small, throwaway-friendly patch improves field
experience now; the Cinder rework will replace this CSS wholesale and absorb the loss.

Folds in LEDGER item **"Dashboard mobile responsiveness: horizontal overflow on narrow
viewports"**.

## Definition of Done

### Scenario 1: handshake list fits a 360 px viewport without horizontal page scroll

- **Given** the dashboard is loaded on a viewport 360 px wide (typical phone)
- **When** the operator opens the Handshakes view
- **Then** the page body does not scroll horizontally (`document.body.scrollWidth <= window.innerWidth`)
- **And** the four-column table is reachable, either reflowed to fit or scrollable inside its own
  container, not inside the viewport

### Scenario 2: captures and wardriving lists behave the same

- **Given** the dashboard is loaded on a 360 px viewport
- **When** the operator opens the Captures view, then the Wardriving view
- **Then** neither view causes horizontal page scroll
- **And** the table rows render on one line inside a scroll-containing wrapper

### Scenario 3: dashboard grid reflows to one column on narrow viewports

- **Given** the dashboard is loaded on a 360 px viewport
- **When** the Dashboard stat cards render
- **Then** the four stat cards stack vertically (one per row), not clipped or side-overflowing

### Scenario 4: laptop layout unchanged

- **Given** the dashboard is loaded on a 1280 px viewport
- **When** any view renders
- **Then** the layout matches the pre-slice baseline: 260 px aside, main padding 2 rem, grid in
  multiple columns, tables at full width without a scroll wrapper visible

## Design

### What exists and what moves

The current responsiveness surface in `dashboard_dev/index.css` (reviewed before editing):

- `body { overflow: hidden }` + `.app-container { overflow: hidden }` prevents the page
  from scrolling but hides overflow instead of handling it, so content that pushes past
  the viewport gets clipped at the edge rather than contained.
- A `@media (max-width: 768px)` block handles the sidebar transition (fixed drawer,
  hamburger toggle) but does NOT touch `.file-list`, `.card`, `.grid`, or `main` padding.
  So below ~400 px, the tables keep 1 rem cell padding + 4 columns, and `main` keeps
  2 rem padding — which together exceed the viewport.
- `.grid { grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)) }` means a 360 px
  viewport renders a single 300-px column with 60 px of slack; usable on the stat grid,
  but close.

### What the patch does

Four CSS-only moves, no JS, minimal HTML churn (one wrapper per table):

1. **Scroll-contain each table.** Wrap `.file-list` in a `<div class="table-wrap">` with
   `overflow-x: auto` and `-webkit-overflow-scrolling: touch`. The row stays coherent and
   the operator swipes left-right inside the wrapper, which is standard mobile pattern
   for data tables that do not usefully reflow. Three tables (handshakes, captures,
   wardriving) get wrapped in the same idiom.
2. **Tighten `main` and `.card` padding below 600 px.** `main`: 2 rem → 1 rem (and the
   mobile-top offset tracks). `.card`: 1.5 rem → 1 rem. Enough to buy back the overflow
   margin.
3. **Tighten table cell padding below 600 px.** `.file-list th/td`: 1 rem → 0.6 rem
   vertical / 0.5 rem horizontal. Rows stay legible; the wrapper handles any remainder.
4. **Reflow the stat grid below 600 px.** `grid-template-columns: 1fr` so the four cards
   stack. No horizontal risk from the grid itself any more.

Plus one belt-and-braces guard: `body { overflow-x: hidden }` kept, and add
`main { max-width: 100% }` so a child that mis-sizes cannot push the layout.

### Why not just reflow tables into cards

The row-as-card pattern (each `<tr>` becomes a stacked block with labels per cell on
narrow viewports) is prettier but duplicates the label text into CSS pseudo-content and
needs maintenance on every new column. Captures has 3 cols, Handshakes 4, Wardriving 2
— three different label sets. The scroll-contain pattern costs one wrapper div, no
pseudo-content, and does not drift when a column is added. If the Cinder rewrite still
wants per-row reflow, it can build it from scratch; this patch optimizes for throwaway.

### What was considered and NOT reused

- Searched for an existing dashboard media-query block beyond the 768 px sidebar one:
  none. The responsiveness surface in this file is the 768 px block alone.
- Considered extracting a shared `.data-table` helper covering the three list tables
  plus the Wardriving CSV table — rejected as rule-of-three premature (three structural
  tables + one structurally different CSV table), and the Cinder rework will replace all
  of this anyway.
- Considered adding a `window.resize` listener in `dashboard.js` to switch table rendering
  mode — rejected: CSS media queries do the job and keep JS free of layout logic.

### Boyscout: stale label

`dashboard_dev/index.html` line 160 still reads "Enable HTTP Basic Auth" since slice-0171
replaced Basic auth with a form-based session login. Fix in the same slice: label becomes
"Enable dashboard login" to match what the checkbox actually gates after slice-0171.

## Verification

- **Manual device verification** (the acceptance is viewport layout, which is not
  unit-testable): operator loads the embedded dashboard on a phone (360-390 px typical),
  confirms Scenarios 1-4 against each view. Reported back in `## As built`.
- **Dashboard XSS regression gate** (`scripts/dashboard-xss-escaping.test.mjs`) stays
  green: the patch adds a wrapper `<div>` but does not touch any
  `escapeHtml`-bearing path, and the three `innerHTML` call sites in `dashboard.js`
  (`handshake-list`, `capture-list`, `wardriving-list`) are untouched.
- **Native suite** (`pio test -e native`): patch is CSS + static HTML only, so the suite
  is confirmed unaffected rather than re-covered by new tests. Running it still confirms
  the embed path (login.html / dashboard.js / index.html / index.css) is picked up by
  `board_build.embed_files` without a filename regression.

## As built

To be filled at merge.
