---
id: '0087'
title: "A shared ActionButton for the green start-operation rows"
type: architecture
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Context

ADR-0085 named three UI popup/row primitives and shipped two of them (`ActionMenu`,
`ConfirmDialog`), deferring the third, "the inline action-button row," to the Shared
action-button ledger item. This ADR is that third primitive, scoped to what the evidence
below actually supports.

A 2026-10-20 sweep of every screen in `src/ui/screens/` for "start / run / execute an
operation" affordances found **12 in-screen band rows** (plus footer-hint legends and
action-menu items, which are not in-screen rows and are out of scope). The earlier ledger
guess of three idioms (banded `>> TEXT <<`, bandless `>> START <<`, rounded-rect buttons)
was wrong once measured: there is no bandless idiom and no rounded-rect idiom left; all 12
are full-width `fillRect`/`ScrollableList` bands. The 12 fall into three measured
populations:

- **Group A, green bespoke inline band, carrying a readability bug (3 sites):** rfid
  `>> START AUDIT <<` (`rfid_screen.cpp:415`), rfid `>> START WRITE <<`
  (`rfid_screen.cpp:1404`), and handshake `>> START AUTO HUNT <<`
  (`handshake_screen.cpp:1106`). Each draws its own band with `fillRect(..., theme::SUCCESS())`
  and sets the selected text to `theme::TEXT_PRIMARY()`.
- **Group B, accent/`BG_SECONDARY` bespoke inline band, no bug (4 sites):** handshake
  `>> START CAPTURE <<`, evil_twin `>> START <<`, karma `>> START <<`, ble_bad_ble
  `[ START ATTACK ]`. Each is the **terminal row of a config form**.
- **Group C, already on the shared `ScrollableList` band (5 sites):** ble_spam, deauth,
  probe_flood, beacon_spam, sniffer `Start Capture`. Also config-form terminal rows,
  already migrated to the list component's `BG_SELECTED()` band as their last row.

Two defects are specific to Group A, and both are measured:

1. **The selected-state contrast bug is theme-dependent and in one theme catastrophic.**
   `theme::SUCCESS` is a fixed, theme-independent pure green `rgb565(0x00, 0xFF, 0x00)`
   (`theme_manager.h:216`). The selected text is `theme::TEXT_PRIMARY()`, which is
   per-theme: white `0xFFFFFF` in the default theme (low contrast on bright green), but
   matrix green `0x00FF41` in the green theme (`theme_manager.h:122`), i.e. green text on
   green fill, effectively invisible. Pairing a fixed fill with a per-theme text is the
   root cause.
2. **Each site hand-rolls the band geometry.** The band height is a local `rowH` (13 in
   rfid, hardcoded 14 in handshake), never the `theme::LIST_ROW_HEIGHT` token, and the
   label is drawn at the band top with no vertical centring, unlike every migrated list
   row (which centres with `theme::LIST_ROW_TEXT_DY`).

Groups B and C are config-form terminal rows: they live at the bottom of a `< value >`
config list, and five of them already share `ScrollableList`. The config-form restyle
(ledger item 91) already owns the decision to "settle one reference config look" and
records that "the terminal action row overlaps the Shared action-button item: design the
two together." Restyling them here, ahead of that decision, would pre-empt it.

## Decision

Add a shared **`ActionButton`** component (`src/ui/components/action_button.h`, header-only)
and route the **three Group A standalone green start rows** through it, deleting their
bespoke inline bands and local `rowH`. The component renders the standalone "start an
operation" row:

- **One guaranteed-contrast look, keeping the green "go" identity.** Selected draws a
  full-width `theme::SUCCESS()` band with `theme::BG_PRIMARY()` text; unselected draws
  `theme::SUCCESS()` text on no band. The fix for the bug is to pair the fixed-green fill
  with `theme::BG_PRIMARY()`, which is near-black in every theme by design (`0x0000`
  default and matrix, `0x0A0000` T-800, `0x000A05` green-T), so the fill/text pair is
  high-contrast in every theme, unlike the old per-theme `TEXT_PRIMARY`. This is a
  root-cause fix (wrong text token), not a restyle: the row stays green, because green is
  the firmware's learned "start/go" signal and every unselected instance already is green.
- **The component owns the band geometry.** It draws the band at `theme::LIST_ROW_HEIGHT`
  and centres the label with `theme::LIST_ROW_TEXT_DY`, killing the per-site `rowH` drift
  and the missing vertical centring in one place.
- **The component owns the `>> LABEL <<` framing.** Call sites pass the bare verb
  (`"START AUDIT"`); the component frames it, so the decoration is consistent and is not
  re-spelled at each site.
- **A render helper, not a stateful component.** Unlike `ConfirmDialog`, `ActionButton`
  has no interaction model and holds no state: the selection already lives in each screen's
  existing config/prompt index. `render` is a template over the device canvas (instantiated
  only on device, like `ActionMenu::render`); the one piece of host-testable logic, the
  label framing, is a free function with a native unit test. This is the simplest thing
  that removes the duplication (KISS).

**Scope boundary, handed off like ADR-0085's was.** The config-form terminal rows (Groups
B and C, including handshake `>> START CAPTURE <<`) are **not** touched here. They are
unified with the config-form restyle (ledger item 91), which already owns the one-reference-
config-look decision and the "design the two together" note. Just as ADR-0085 delivered the
whitelist confirm (idiom 3) so this item must not touch it, this ADR leaves the config-form
rows to item 91 so the two efforts are sequenced and each row is restyled exactly once.

Why green rather than folding Group A into the accent config look now: the three Group A
rows are standalone action rows on prompt/mode screens, not the terminal row of a config
list, so the config-list treatment does not structurally fit them; and the only measured
defect is the selected text token, so correcting that one token is the minimal correct fix.
Whether the firmware should ultimately have one action-row look or two (green standalone vs
accent config-terminal) is item 91's call when it settles the config look; this ADR does not
foreclose it, it defers it with the boundary recorded.

Why not build the accent variant now: there is no present need in Group A (3 green sites =
exactly the rule-of-three trigger for the green look), and a second style used only by
future item-91 work would be speculative generality. The component is written so a second
style can be added without reworking the call sites, but the accent path is added when item
91 needs it, not before.

## Consequences

- **The three green start rows become readable in every theme**, including the green theme
  where the selected label was previously green-on-green, and the accidental low-contrast
  white-on-green in the default theme is gone.
- **DRY.** Three hand-rolled `fillRect(SUCCESS)` bands with three different local heights
  collapse into one component with one test. New standalone start rows reuse it instead of
  copying a neighbour.
- **The third modal/row primitive exists**, completing the set ADR-0085 named: `ActionMenu`
  (pick one of N), `ConfirmDialog` (confirm or cancel), `ActionButton` (start an operation).
- **Two action-row looks remain by design**, green standalone and accent config-terminal,
  until ledger item 91 decides whether to fold them. The boundary is documented here and in
  that item, so neither effort restyles the other's rows.
- **Testable logic, device-only render.** The label framing is host-tested (native Unity);
  the `render(Canvas&)` template is covered by build plus on-device review, exactly as
  `ActionMenu::render` and `ConfirmDialog::render` are.
- **Per-site migration carries behaviour risk needing on-device verification.** Replacing
  each screen's inline band with the component changes its render path, so each of the three
  rows (selected and unselected, and in the green theme specifically for the contrast fix) is
  verified on hardware before the slice's As built is frozen; any residual is tracked in the
  ledger.
