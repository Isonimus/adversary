---
id: '0113'
title: "Drop the mode echo from the Beacon and Probe status bars"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

The Beacon Spam and Probe Flood screens rendered the current mode name (RickRoll, Random,
Targeted, ...) as status-bar center content on every frame, in every state. Since slice-0106
the mode is a `Mode:` `ConfigRow` at the top of each config form, so the status-bar copy is a
redundant echo while configuring, and it costs a per-frame text overpaint in the bar on top of
the clutter. Remove it; the config row is the single source.

## Definition of Done

- **Given** the Beacon Spam or Probe Flood screen in any state
- **When** its header is rendered
- **Then** the status bar shows only the title (and the usual right-side indicators), with no
  mode name in the center, and the bar's colors are unchanged.

- **Given** the config state of either screen
- **When** it is rendered
- **Then** the mode is still shown by the `Mode:` config row, so the mode remains visible where
  it is set.

## Design

Both `drawHeader` calls passed `(canvas, title, modeName, BG_SECONDARY, TEXT_PRIMARY)`. The two
color arguments were exactly the render defaults, so the call collapses to
`StatusBar::render(canvas, title)` with no visual change to the bar beyond dropping the center
text. `getModeName()` stays: it still feeds the `Mode:` config row.

The mode is a pre-start creative/targeting choice, not live attack telemetry, so dropping it from
the running/paused headers loses no actionable run-time state: the Beacon running view already
streams the current SSID (which reveals the themed modes), and the Probe running view already
shows a Target line in targeted mode. Keeping the `Mode:` row as the single source matches that
the mode is set once, before Start.

Sniffer was deliberately left unchanged. Its status-bar mode is the active capture filter (ALL /
BEACON / PROBE / EAPOL / ...), which is live context the operator needs while capturing, not a
pre-start echo of a config row, so it is not the same redundancy.

## Verification

- `pio run -e cardputer` and `pio run -e m5stick`: both SUCCESS (built serially).
- No native-testable logic changed (device-only render path; a redundant render argument was
  removed, not a behavior with a decision in it). On-device check: the Beacon and Probe headers
  show only the title in config, running, and paused, and the config `Mode:` row still shows the
  mode.

## As built

As designed. Files: `src/ui/screens/beacon_spam_screen.cpp`, `src/ui/screens/probe_flood_screen.cpp`
(each `drawHeader` only).
