---
id: '0096'
title: "Unify config-form rows on one chrome-only ConfigRow"
type: architecture
status: accepted
date: 2026-10-21
supersedes: []
superseded_by: []
---

## Context

Four screens draw a config form as fixed `label ... value` rows the operator steps in place
(ADR-0092 keys): `evil_twin` (`renderConfig`), `karma` (`renderConfig`), `ble_bad_ble`
(`drawIdle`), and `handshake` Manual (`drawConfig`, after ADR-0094 gave it a fixed layout).
Three of the four moved their scroll/selection mechanism onto the shared `ScrollableList`
in slice-0091, but each kept its own row *content* drawing verbatim, so the house look has
drifted on every axis. Measured 2026-10-21:

| Form | Label X | Value position | Value colour | Chevrons |
|------|---------|----------------|--------------|----------|
| `settings` (canonical ~50-row list) | 8 | right-aligned, 8px margin | dim `TEXT_SECONDARY` | none |
| `handshake` Manual | 4 | right-aligned, 8px margin | `TEXT_PRIMARY` | none |
| `evil_twin` | 4 | fixed x=80 | inherits label (`ACCENT` when selected) | none |
| `karma` | 4 | fixed x=75 | inherits label (`ACCENT` when selected) | none |
| `ble_bad_ble` | 10 | fixed x=80 | two-tone `PRIMARY`/`SECONDARY` | `< value >` |

Four independent copies of the same `label + value` idiom is the rule-of-three trigger, and
the drift is visible side by side. The design framing was fixed in `LEDGER.md` on
2026-10-20: a new row *component* is earned by a genuinely different in-row **shape** (a
stepper, a gauge), not per value **type** (toggle/range/switch are formatters of one shape,
kept at the call site). `settings_screen`'s `SettingType` union is the counter-example that
earns its place only because it is a data-driven ~50-row list, not a hand-written form.

`settings` is also the strongest precedent for the look itself: it is the largest value list
in the firmware, it right-aligns its value with an 8px margin (`VALUE_RIGHT_MARGIN`), labels
at x=8 (`ROW_TEXT_X`), and draws the value dim (`TEXT_SECONDARY`) with selection shown by the
list band alone. `handshake` already matches its value alignment. The two biggest lists
agree; the three migrated forms are the outliers.

## Decision

**Build one chrome-only `ConfigRow` render helper and route all four forms' uniform value
rows through it, settling a single house look taken from the canonical `settings` list.**

- **Layout.** Label at x=8; value right-aligned with an 8px margin. These are
  `settings`' own constants, already matched by `handshake`. The three migrated forms' fixed
  value columns (75/80) and their 4/10 label x collapse onto this one convention.
- **Colour.** Label `TEXT_PRIMARY`, value dim `TEXT_SECONDARY`. **Selection is shown by the
  row band only** (the shared list's `BG_SELECTED`), never by recolouring the row's text.
  Because the text colours no longer depend on selection, `ConfigRow::render` takes **no
  `selected` parameter**: the caller draws the band, `ConfigRow` draws the text on top. This
  drops `evil_twin`/`karma`'s ACCENT-on-select recolour (redundant with the band),
  `ble_bad_ble`'s two-tone value, and makes `handshake`'s value dim like the rest.
- **No chevrons.** The value is a plain right-aligned string, exactly as `settings` draws it.
  `ble_bad_ble` loses its `< value >`. The step affordance comes from the selection band plus
  the footer hint; adding chevrons would spread a ~4-char width cost to 10+ rows across four
  screens on the narrow M5StickC display, where `settings` proves they are unnecessary.
- **Content stays at the call site.** `ConfigRow` is chrome only: it positions and colours a
  `label` and a `value` string. The value string (an `ON`/`OFF` toggle, a `60s` range, a
  `Keyboard` switch) is formatted by the screen, because those are formatters of the one row
  shape, not distinct shapes. No per-type logic enters the component.
- **The band is not the component's job.** For the three `ScrollableList`-backed forms the
  component already paints the selection band before the row callback runs; for `handshake`
  (deliberately off `ScrollableList`, ADR-0094 / ledger) the screen paints its own band.
  `ConfigRow` drawing a band too would double it. To make the dim-value look legible and
  consistent, `handshake`'s hand-drawn selected band changes from the bright `ACCENT()` fill
  to the house `BG_SELECTED()` that the migrated forms get from the component; the `START`
  `ActionButton` keeps its accent "go" band (ADR-0090), and `handshake`'s bespoke Target row
  (with its `WARNING` empty state) keeps its own colours and is **not** routed through
  `ConfigRow`.
- **No value-colour override is added.** No current call site needs a semantic value colour
  (none of the four colour `ON` green today), so the component exposes none; a future
  semantic need adds it on the rule of three, not speculatively.

`ConfigRow` is header-only with a `render(Canvas&)` template (device-only, like
`ActionButton`/`ActionMenu`) and one pure, host-testable helper for the right-aligned value
X (`configRowValueX`), mirroring `ActionButton`'s `formatActionLabel`.

## Consequences

- **One definition of the config-row idiom.** The label X, value alignment, and the two text
  colours live in `config_row.h`; the four forms can no longer drift apart, which is the
  whole point of the convergence arc (slice-0089 start rows, slice-0091 scroll mechanism,
  this slice the content layer).
- **Visible changes, all toward the canonical look:** `ble_bad_ble` loses chevrons and its
  two-tone value; `evil_twin`/`karma` values move to right-aligned and stop recolouring on
  select; `handshake`'s value goes dim and its config-row band softens from `ACCENT` to
  `BG_SELECTED`. On-device verification confirms each form still reads cleanly and the dim
  value stays legible on the band (as it does in `settings`).
- **The component carries no state and no `selected` flag**, so it composes identically
  inside a `ScrollableList` row callback and inside `handshake`'s hand-stepped loop. The only
  adapter is that `handshake` passes a row-top `y` offset by the house text baseline, so its
  existing per-row spacing is preserved.
- **Scope held:** `handshake`'s Target row and all `START` rows stay as they are
  (`ActionButton` already unified the starts in slice-0089). This slice is strictly the
  middle value rows.
