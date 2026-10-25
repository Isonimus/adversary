---
id: '0124'
title: "Compose the live version into the What's New title line"
type: slice
status: accepted
date: 2026-10-25
supersedes: []
superseded_by: []
---

## Goal

Make the What's New screen's first (title) line read `v<VERSION> - <CODENAME>` (for example
`v0.6.0-alpha - FIELD USABILITY`), so the release identity and its version sit together as the
prominent title. The version stays sourced live from `config::VERSION`, never hard-coded into
the notes body (ADR-0121), and the now-redundant status-bar version subtitle is removed so the
version shows exactly once.

## Definition of Done

- **Given** the What's New screen
- **When** it is rendered
- **Then** line 0 shows `v` + `config::VERSION` + ` - ` + the `whats_new.h` codename line, in the
  accent color.

- **Given** the status bar
- **When** it is rendered
- **Then** it shows `WHAT'S NEW` with no version subtitle (the version now lives in the title).

- **Given** VERSION is bumped and the codename line is left unchanged
- **When** the screen renders
- **Then** the title shows the new version automatically, with no `whats_new.h` edit needed for
  the version half.

## Design

ADR-0121 keeps the notes body version-agnostic: the version is shown live, never typed into
`whats_new.h`. This slice only moves where "live" appears, it does not weaken that rule.

- `drawContent` composes the `i == 0` title line by chained `canvas.print` of `"v"`,
  `config::VERSION`, `" - "`, then `whats_new.h` line 0 (the codename), on one line. The file's
  first line therefore stays the codename only.
- `drawHeader` drops the version subtitle (passes `nullptr` to `StatusBar::render`, the same
  null-subtitle path wardriving already uses), since the version now reads once, in the title.
- `whats_new.h`'s header comment and the CLAUDE.md release checklist are updated to state the
  first line is the codename only and the title prepends the version, so a future release does
  not reintroduce the version into the body (which would render `v0.6.0 - v0.6.0 ...`).

No new state and no new branch beyond the existing `i == 0` case: this is display composition
only.

## Verification

- Device-render-only change (`drawContent` / `drawHeader`, under the screen's ESP32 render
  path); no host-testable logic added, so the native suite count is unchanged.
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS.
- On-device: the title reads `v0.6.0-alpha - FIELD USABILITY` and the status bar shows no
  separate version.

## As built

As designed. `drawContent` composes the `i == 0` title with chained prints (`"v"`,
`config::VERSION`, `" - "`, codename), and `drawHeader` passes `nullptr` as the subtitle so the
status bar shows the screen name only. `whats_new.h`'s header comment and the CLAUDE.md release
checklist (step 3) now state the first line is the codename only, with the version composed live
in front of it.

Verification run this change:
- `pio test -e native`: unchanged (920/920); no host-testable logic added.
- `pio run -e cardputer -j 2`: SUCCESS. RAM 26.5% (unchanged), Flash 75.6% (2,525,475 B, +460 B
  over slice-0123's 2,525,015 B, the four extra prints + literals).
- `pio run -e m5stick -j 2`: SUCCESS. RAM 21.0% (unchanged), Flash 78.5% (2,468,283 B, +488 B
  over 2,467,795 B).
- Docs: index + lint green (123 documents).
- Note: the two size figures above were each measured from a clean per-env relink run serially;
  an earlier pass accidentally relinked the two envs concurrently (SUCCESS, but against the
  serial-build invariant). Harmless here because nothing reinstalled, but recorded as the slip
  it was.
