---
id: '0089'
title: "Unify the pure start rows onto the shared ActionButton"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

ADR-0087/slice-0088 shipped `ui::ActionButton` (the full-width green `>> VERB <<` start
row: `SUCCESS()` band, `BG_PRIMARY()` text, centred both ways) and migrated the three
standalone green rows (rfid `START AUDIT`/`START WRITE`, handshake auto-hunt
`START AUTO HUNT`). It explicitly handed the remaining start affordances to ledger item 91.

A fresh sweep (2026-10-20) measured those affordances: **nine** start rows drawn **three
different ways** (`[ START ATTACK ]`, `>> START <<`, `[START]`, bare `Start X`), in **three
alignments** (centred, left-aligned, left-label + right-value), in **two colour families**
(every bespoke one is `ACCENT`/`BG_SECONDARY`; only the already-migrated three are green).
This slice folds the subset that is a **pure, non-toggling start action** onto the shared
`ActionButton`, so every "go" affordance in the app reads identically. It also lands
**ADR-0090**: the component's fill moves from the fixed out-of-theme `SUCCESS()` green to the
per-theme `theme::ACCENT()` (the green only fit 2 of 7 themes on-device), and the labels take
a `START <ACTION>` form (`START KARMA`, `START SPAM`, ...) instead of a bare `START`. It does
**not** restyle the config-row idiom (the `< value >` cycle rows and their selection bands);
that stays as a separate ledger follow-on.

Migrated here (7 rows): `ble_bad_ble` `[ START ATTACK ]`, `karma` `>> START <<`,
`evil_twin` `>> START <<`, `handshake` manual `>> START CAPTURE <<`, `ble_spam`
`>> START <<`, `beacon_spam` `[START]`, `probe_flood` `[START]`.

Deliberately **not** migrated (handed to new ledger lines, see Design):
- `deauth` `Start Attack` + right-aligned target value: a value-bearing row, not a button.
- `sniffer` `Start/Stop Capture` and `server_menu` `Start/Stop Server`: Start/**Stop**
  toggles; a green "go" band on the Stop state is semantically wrong and there is no
  stop/neutral variant. They are a distinct two-state-affordance design, not this restyle.

## Definition of Done

- **Given** any of the 7 migrated screens in its config/idle state
- **When** the start row renders
- **Then** it shows the framed `>> VERB <<` label centred across the full width, never the
  old `[ START ATTACK ]`, `[START]`, or a left-aligned `>> START <<`.

- **Given** the start row is the selected row
- **When** it renders
- **Then** it is a `theme::ACCENT()` band (the active theme's primary colour) with
  `theme::BG_PRIMARY()` text, legible in every theme (dark text on the always-bright accent).

- **Given** the start row is not selected
- **When** it renders
- **Then** it is `theme::ACCENT()` text on no band.

- **Given** all 7 migrated screens
- **When** their start rows render
- **Then** each is the identical `ui::ActionButton` idiom, visually matching the already
  shipped rfid/handshake-auto-hunt rows.

- **Given** the operator navigates and activates the start row
- **When** the selection lands on it and ENTER is pressed
- **Then** the row is selectable at its existing index and triggers the same action as
  before; only its rendering changed (no interaction or selection-model change).

## Design

**Measured basis.** The 2026-10-20 sweep catalogued every start row (file:line quoted in
the ledger history). Seven are pure `START X` rows that never toggle to another verb; those
are the migration set. The green `ActionButton` is already proven on-device: slice-0088
shipped it for rfid `START AUDIT`/`WRITE` and handshake `START AUTO HUNT`, the last of which
lives **inside a config screen** (`handshake_screen.cpp` drawConfig) and was operator-verified.
So green-for-go in a config context is not speculative, it is an approved precedent.

**Decision: one component, no colour variant; its single fill is the theme accent.** Ledger
item 91 predicted the config-form start rows would "likely need an accent/non-green variant"
branching off a green base. Neither half survived the device: there is still exactly one
`ActionButton` with no variant (a parameterised second colour would be speculative generality,
global §3), but its base fill moved from the fixed `SUCCESS()` green to `theme::ACCENT()`. The
fixed green was out of theme in five of seven themes (ADR-0090); the accent is each theme's
own primary-action colour, so one accent idiom reads as "start" in every theme without a
second variant.

**What was considered and reused.** `ui::ActionButton::render(canvas, y, "VERB", selected)`
is reused as-is apart from retokenising its fill to `theme::ACCENT()` (ADR-0090): it already
owns the frame (`>> %s <<`), the full-width centring, the accent/`BG_PRIMARY` contrast pair,
and the `LIST_ROW_HEIGHT`/`LIST_ROW_TEXT_DY` geometry. Each call site keeps its own `selected`
flag and the `y` of its start-row slot; the only judgement per screen is passing the band
**top** `y` (the old rows drew their band at `y-2` with a bespoke `lineHeight`, whereas
`ActionButton` draws a `LIST_ROW_HEIGHT` band from `y`), which on-device verification confirms
aligns with the row slot. No new component code, no new constants, no test-surface change.

**What was deliberately not reused / not done.** The config-*row* idiom (selected-band token
drift: 3/4 forms use `BG_SECONDARY`+`ACCENT`-text, `handshake` is the `ACCENT`-fill outlier;
and the `< value >` framing that only `ble_bad_ble` uses) is **out of scope** here: it is a
separate visual concern and bundling it makes an oversized slice. The two Start/Stop toggles
and the value-bearing `deauth` row are excluded for the semantic reasons above. All three go
to the ledger so the thread is not lost.

## Verification

- **Native suite stays green** (`pio test -e native`): `test_action_button` (6/6) already
  pins `formatActionLabel`'s framing/truncation contract, and `test_confirm_dialog` guards
  the neighbouring component. This slice adds no new pure logic (it is call-site reuse of an
  already-tested component), so the proof that the contract is intact is that these keep
  passing with the migration applied.
- **Both device builds SUCCESS**, built serially: `pio run -e cardputer`, then
  `pio run -e m5stick`.
- **ADR-0083 warnings gate** clean on both build logs:
  `node scripts/lint-build-warnings.mjs <build.log>`.
- **On-device visual check** (the real proof for a render change): on each of the 7 screens,
  the start row shows `>> VERB <<` centred, green band + dark text when selected, green text
  when not, and the band aligns with its row slot (the one `y`-placement risk). Switch themes
  (e.g. T-800 red, Cyberpunk pink, M5 orange) and confirm the band now matches the theme
  accent and the dark label stays legible, instead of the old fixed green (ADR-0090).
  `evil_twin`'s form scrolls, so also confirm the row paints correctly when scrolled into view.

## As built

Shipped as scoped. The seven pure start rows now render through
`ui::ActionButton::render`: `ble_bad_ble` `START ATTACK`, `karma` `START KARMA`, `evil_twin`
`START TWIN`, `handshake` manual `START CAPTURE`, `ble_spam` `START SPAM`, `beacon_spam`
`START BEACONS`, `probe_flood` `START FLOOD` (ten `ActionButton` call sites in all, with the
three from slice-0088). The three competing frames, three alignments and two colour families
collapsed to one framed, centred, accent idiom.

Mid-slice, the on-device theme review turned up what the automated checks could not: the
fixed `SUCCESS()` green was out of theme in five of the seven themes. The component's fill was
retokenised to `theme::ACCENT()` and the reversal recorded in **ADR-0090** (supersedes
ADR-0087). The three rows shipped under slice-0088 re-tinted to accent with no call-site
change, which is the consistency payoff. Labels took the `START <ACTION>` convention.

Excluded as planned and handed to the ledger: `deauth`'s value-bearing `Start Attack` row (no
value slot on a button) and the `sniffer` / `server_menu` `Start/Stop` toggles (a 'go'-coloured
band on a Stop state is wrong, and no two-state variant exists). The config-*row* idiom restyle
stays as the remaining half of ledger item 91.

Verification met every Definition-of-Done scenario: native suite 858/858 (incl.
`test_action_button`, unchanged: the colour lives in the untestable render path, the framing
contract it pins is intact); cardputer + m5stick both SUCCESS, built serially; ADR-0083
warnings gate clean on both logs; docs index + lint green. On-device the operator confirmed
the framed labels, the band alignment on the four hand-rolled screens, the accent band
matching the theme across T-800/Cyberpunk/M5 themes with the dark label legible, and the
`evil_twin` scrolled case. No device-verify residual was filed.
