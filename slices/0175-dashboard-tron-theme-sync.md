---
id: '0175'
title: "Dashboard carries the device Tron theme palette"
type: slice
status: accepted
date: 2026-11-27
supersedes: []
superseded_by: []
---

## Goal

Close LEDGER's "Tron theme missing from the dashboard" paper-cut (line 212, ADR-0102):
the device UI has had the Tron blue palette as `ThemePreset::TRON = 6` since its
introduction, but the dashboard only ships CSS for themes 0..5 (Red Team, Matrix,
T-800, Fallout, Cyberpunk, M5Stick). When a Tron-themed device serves the dashboard,
`/api/info.system.theme` returns `6` and the dashboard JS tries to apply
`body.theme-tron` which no CSS rule matches, so the dashboard silently falls back to
the generic `:root` red palette. Visual mismatch with the device for a tester whose
first impression is the dashboard.

This slice adds the missing palette and lists Tron in the dashboard's theme-name
array, so the plumbing that already exists end-to-end finally covers all seven
device presets. Same files as slice-0172, same v0.12.0 "DISCOVERABILITY / SETUP"
cycle: paper-cuts the first-time tester notices.

## Definition of Done

- **Given** a device whose `settings.display.themePreset` is `ThemePreset::TRON` (6)
- **When** the operator opens the dashboard in a browser
- **Then** on the first `/api/info` response the dashboard applies `body.theme-tron`,
  the body background renders black, headings and active accents render cyan, and
  no console warning is emitted about an unknown theme index

- **Given** the operator switches device theme from Settings while the dashboard is
  live
- **When** the next poll cycle receives a new `system.theme` index
- **Then** the dashboard swaps `body.className` to the matching theme class for any
  of the seven presets (not just six), with Tron rendering the new palette

- **Given** a device still on Red Team (or any other existing preset)
- **When** the dashboard loads
- **Then** behaviour is unchanged (no regression on the six existing palettes)

## Design

Searched: `dashboard_dev/index.css` for `body.theme-*` blocks (6 present),
`dashboard_dev/dashboard.js` for the `themeNames` array (6 entries, indexed from
`system.theme`), `src/modules/server/server_manager.cpp` for the `/api/info`
payload (already exposes `settings.display.themePreset` under `system.theme`),
`src/ui/theme_manager.h` for the canonical palette values (Tron is the only
blue-identity theme in the device, electric cyan on black — `accent #00f0ff`,
`accent-light #80ffff`, `accent-dark #008080`, `bg #000000`, text `#00f0ff` /
`#009090`, border `#003030`). Reused: all three (CSS, JS, server) — the gap is
pure content, not infrastructure. Deliberately NOT reused: login.html's inline
palette (lines 15-25) stays red-themed by design. Login is pre-auth and
pre-session, so theming it would need a new public (unauthenticated) GET surface
exposing the device theme, which widens the auth surface for a two-second visible
page. The ledger line notes login.html in passing but a theme leak on the
login screen is a separate decision: fold it into either (a) a tiny public
`/api/public-theme` endpoint if a future slice decides it's worth it, or (b) the
Cinder dashboard rewrite that is likely to re-template login anyway.

Rule of three: there are now three places palette tokens appear (`index.css`
`:root`, 7 `body.theme-*` blocks, dashboard JS `themeNames`). Still not worth a
shared config file — the tokens are declarative CSS, the JS array is 7 strings;
extracting a build-time palette JSON would add a build step we deliberately do
not have (the dashboard is zero-build per the embed_files design).

## Verification

- `pio test -e native` stays green. No native test for CSS / JS content (the
  behaviour under test is the rendered palette, which can only be verified in a
  browser against a running device); if an existing dashboard JS test covers the
  theme-name array range, update it.
- `pio run -e m5stick -j 2` and `pio run -e cardputer -j 2` link clean (the
  dashboard sources are embedded into the firmware via `board_build.embed_files`
  per slice-0169; the Tron CSS adds roughly one `body.theme-tron { ... }` block
  to the embedded blob).
- Manual: load the dashboard in a browser, set `system.theme` to each of 0..6 via
  Settings on-device, confirm the palette swaps including Tron on 6.

## As built

Shipped as designed. `dashboard_dev/index.css` grew a `body.theme-tron { ... }`
block with the electric-cyan-on-black palette matching `ThemePreset::TRON` in
`src/ui/theme_manager.h`. `dashboard_dev/dashboard.js`'s `themeNames` array
grew a `'tron'` entry at index 6. Login page left on its red palette per the
pre-auth caveat in Design. Both xtensa builds clean; native suite green.
Stale LEDGER line 211 (dashboard horizontal-overflow, closed by slice-0172)
deleted in the same commit as janitorial cleanup. Line 212 closed by this
slice. v0.12.0 is now code-complete; only BTM PMF-survival carries over.
