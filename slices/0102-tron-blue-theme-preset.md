---
id: '0102'
title: "Tron blue theme preset"
type: slice
status: accepted
date: 2026-10-21
supersedes: []
superseded_by: []
---

## Goal

The theme roster has red x2 (Red Team, T-800), green x2 (Matrix, Fallout), a pink-dominant
Cyberpunk, and orange M5Stick, but no cyan/blue identity theme: the only blue in the file is
`CUSTOM_DEFAULT`, which is not user-selectable. Add a Tron-styled electric-cyan-on-black
preset to fill that gap, choosing every colour so it survives the 8-bit RGB332 canvas (the
same constraint that reshaped Cyberpunk's background ramp).

## Definition of Done

- **Given** the Settings theme selector
- **When** the operator cycles to the new entry
- **Then** it reads "Tron" and the UI repaints in electric cyan on a near-black void:
  bright cyan accents, a teal selection band, cyan-tinted white primary text.

- **Given** a device with a previously saved `themePreset` of 0..5
- **When** it boots after this change
- **Then** it loads the same theme as before (Red Team..M5Stick indices are unchanged;
  Tron is inserted at index 6, ahead of CUSTOM which shifts to 7).

- **Given** the Tron palette values
- **When** each is quantized to the RGB332 canvas (red/green to 3 bits, blue to 2 bits)
- **Then** every colour lands on a *visible, intended* RGB332 cell (no accent or text colour
  collapses to black, and the three near-black backgrounds land on distinct cells), asserted
  by a host test so a future edit that breaks the landing fails the build.

## Design

**Palette (RGB888 intent -> RGB332 display).** Tron's authentic look is cyan ink on a black
grid void, which also sidesteps the RGB332 blue trap (blue has only 4 levels: 0/85/170/255,
so deep tinted blues quantize to black). The identity therefore lives in the accent and the
selection band, not in a tinted background:

| Role | intent | RGB332 lands | note |
|------|--------|--------------|------|
| bgPrimary | 0,0,0 | black | the grid void |
| bgSecondary | 0,0,96 | 0,0,85 | darkest visible blue panel |
| bgTertiary | 0,48,96 | 0,36,85 | dark teal (hover) |
| bgSelected | 0,80,128 | 0,73,170 | teal selection band |
| accent | 34,211,238 | 36,219,255 | electric cyan |
| accentLight | 144,232,255 | 146,255,255 | pale cyan glow-core |
| accentDark | 0,112,176 | 0,109,170 | teal |
| textPrimary | 176,240,255 | 182,255,255 | cyan-tinted white |
| textSecondary | 64,144,176 | 73,146,170 | muted teal |
| textDisabled | 0,80,96 | 0,73,85 | dark teal |
| textInverse | 0,0,0 | black | text on cyan bands |

**Insertion point.** `TRON` goes at enum value 6, pushing `CUSTOM` to 7 and `PRESET_COUNT`
to 8. This keeps the persisted `themePreset` indices 0..5 stable (a saved Red Team..M5Stick
still loads itself); only a persisted CUSTOM (6) would shift, and CUSTOM is a disabled
placeholder no shipping build exposes.

**Boyscout: drop the preset-count magic numbers.** Adding a preset currently means editing
three hand-counted literals, which is exactly how a new theme goes half-wired. This slice
derives them from the enum so the next theme is a one-line enum edit:
- `ThemeManager::getPresetCount()` returns `PRESET_COUNT`/`PRESET_COUNT - 1` (custom
  on/off) instead of the bare `7`/`6`.
- The Settings selector max becomes `static_cast<int>(ThemePreset::CUSTOM) - 1` (the last
  non-custom index) instead of the bare `5`.
- `SettingsManager`'s `themePreset` comment lists Tron.

**Existing code considered.** The theme system already dispatches every preset through the
`presets::` struct + `loadTheme` switch + `getThemeName` switch; Tron is one more case in
each, no new mechanism. `rgb565()` is the existing RGB888->565 helper; the canvas does the
565->332 step at draw time, so the host test asserts the full 888->332 landing with a small
local quantizer mirroring `>>5`/`>>5`/`>>6`.

## Verification

- Host unit test `test/test_theme/test_theme.cpp`: for `presets::TRON`, quantizing each
  channel to RGB332 lands on the intended cell from the table above (accent -> 36,219,255;
  the three backgrounds distinct and non-black where intended; textPrimary cyan-tinted, not
  pure white). Guards the RGB332-survival invariant. `getThemeName(ThemePreset::TRON)` is
  "Tron"; `PRESET_COUNT` is 8. Run under `pio test -e native`.
- Compile gate: `pio run -e cardputer` and `-e m5stick`, built serially.
- On-device: cycle Settings -> Theme to Tron on the Cardputer; confirm the accent reads as
  bright cyan (not muddy blue, not collapsed to white), the selection band is a visible
  teal, text stays legible, and a saved non-Tron theme still restores itself across a reboot.

## As built

Shipped as designed. `ThemePreset::TRON` inserted at value 6 (CUSTOM -> 7, PRESET_COUNT ->
8), with the `presets::TRON` palette, the `getThemeName` case, and the `loadTheme` case. Each
palette value carries its RGB332 landing as an inline comment, mirroring the CYBERPUNK note.
The preset-count magic numbers were dropped: `getPresetCount()` derives from `PRESET_COUNT`,
and the Settings selector max derives from `ThemePreset::CUSTOM - 1`
(`settings_screen.cpp`), so the next preset is a one-line enum edit. `SettingsManager`'s
`themePreset` comment lists Tron.

Verification: `test/test_theme/test_theme.cpp` (new, 5 cases) asserts the RGB332-survival
invariant on the bucket indices (accent -> electric-cyan cell 1/6/3, no foreground collapses
to black, the four backgrounds land on distinct cells, primary text is cyan-tinted not pure
white, and the preset is registered with PRESET_COUNT == 8), passed under `pio test -e
native`. Full native suite 874/874. Both device builds (`cardputer`, `m5stick`) compiled
clean, built serially. Doc lint clean. On-device on the Cardputer (2026-10-21) the operator
confirmed Tron renders as intended: the accent reads as bright electric cyan, the selection
band is a visible teal, text stays legible, and the near-black backgrounds are distinct.
Saved non-Tron themes still restore across a reboot (indices 0..5 unchanged).
