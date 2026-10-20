---
id: '0090'
title: "ActionButton fills with the theme accent, not a fixed success green"
type: architecture
status: accepted
date: 2026-10-20
supersedes: ['0087']
superseded_by: []
---

## Context

ADR-0087 introduced `ui::ActionButton` for start rows and chose a **fixed**
`theme::SUCCESS()` green band with `theme::BG_PRIMARY()` text, on the premise that green
reads as "go" universally and that a fixed colour guarantees contrast in every theme.

On-device across the theme set that premise proved wrong. `theme::SUCCESS()` returns
`status::SUCCESS` (`theme_manager.h`), a fixed pure green that is theme-blind. But each
theme's signature colour is `.accent`, commented "Primary accent color", and it is not green
in most themes:

| Theme | `accent` | Fixed-green button |
|-------|----------|--------------------|
| Red Team | pure red | out of theme |
| T-800 | terminator red | out of theme |
| Cyberpunk | neon pink | out of theme |
| M5Stick | M5 orange | out of theme |
| Custom | blue | out of theme |
| Matrix | matrix green | fits (by luck) |
| Fallout | pip-boy green | fits (by luck) |

So the green only fit 2 of 7 themes; in the other five it sat on the screen as a foreign
element. The "green = go reads universally" reasoning ignored that the device is themed and
the accent already *is* each theme's primary-action colour.

## Decision

`ActionButton` fills the selected band with `theme::ACCENT()` and draws the label in
`theme::BG_PRIMARY()`; unselected draws `theme::ACCENT()` text on no band.

This keeps ADR-0087's contrast guarantee while making the row theme-aware. `BG_PRIMARY()` is
near-black in every theme (`0x0000` / `0x0A0000` very-dark-red / `0x000A05` very-dark-green /
`0x080050` dark indigo), and `accent` is always a bright saturated colour, so dark-on-accent
(selected) and accent-on-dark (unselected) both read in every theme. The accent is the
theme's primary colour, so the button now matches the theme instead of fighting it, and the
"this is the primary action" signal is preserved per-theme rather than tied to one fixed hue.

The component is otherwise unchanged from ADR-0087: a stateless render helper that draws a
full-width band at a caller-supplied `y`, `theme::LIST_ROW_HEIGHT` tall, with the label
framed `>> VERB <<` and centred both ways; selection stays owned by each screen.

Start-row labels follow a **`START <ACTION>`** convention: the frame names what will start
(`START AUDIT`, `START CAPTURE`, `START KARMA`, `START SPAM`, `START FLOOD`, ...), never a
bare `START`, so the button is self-describing on whichever screen it sits.

## Consequences

- Every start affordance now renders in the active theme. The three rows shipped under
  ADR-0087 (rfid `START AUDIT`/`START WRITE`, handshake `START AUTO HUNT`) re-tint from green
  to accent for free; no call site changes for the colour, since it lives in the component.
- The literal "green"/"SUCCESS" framing in ADR-0087's title and prose is retained for the
  historical record (ADRs are immutable); this ADR is the current source of truth.
- `theme::SUCCESS()` stays correct for small fixed status text (`[RUNNING]`, `[ON]`): that is
  a semantic state indicator, not a themeable action surface, so those sites are unchanged.
- slice-0089 applies this alongside migrating the remaining pure start rows onto the
  component.
