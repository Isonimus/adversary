---
id: '0122'
title: "On-device what's-new screen (auto-show once per update + Settings entry)"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

Implement ADR-0121: show the operator a short, on-device note of what changed in the version
now running, so features shipped to OTA/M5Burner alpha users (who never open the GitHub
`CHANGELOG.md`) are actually noticed. Auto-show it once on the first boot after a version
change, and leave it reachable on demand from Settings.

## Definition of Done

- **Given** a device whose NVS last-seen version differs from `VERSION` (including a fresh
  flash with no stored value)
- **When** it boots to the idle menu
- **Then** the What's New screen appears showing the running version's notes, and NVS is
  updated to `VERSION` so a subsequent reboot goes straight to the menu.

- **Given** the What's New screen is showing
- **When** the operator presses ESC
- **Then** it returns to the main menu.

- **Given** the device has already shown the notes for the running version
- **When** the operator opens Settings and selects "What's New"
- **Then** the same screen opens on demand (the notes stay re-readable after the one-shot).

- **Given** the notes are longer than one screen
- **When** the operator scrolls with Up/Down
- **Then** the view scrolls and shows an "N/total" position indicator, matching the About
  screen.

- **Given** a device running with no SD card (the target audience)
- **When** it is updated and reboots
- **Then** the auto-show still fires, because the trigger marker lives in NVS, not in the
  SD-backed settings (a card is never required for the notes to appear once per update).

## Design

Per ADR-0121. Four pieces:

**Bundled notes** (`src/config/whats_new.h`): a `const char* const WHATS_NEW_LINES[]` for the
running version plus `WHATS_NEW_LINE_COUNT`, operator-pitched and short. Curating it is a new
release-checklist step (added to CLAUDE.md). This release's content summarizes v0.6.0 "Field
usability" (inactivity screen sleep, local-time offset, SD-aware greying).

**Version marker** (`src/core/version_marker.h` + `.cpp`): the NVS plumbing behind one helper,
and the one branch isolated as a pure, host-tested function.
- `inline bool versionDiffers(const char* seen, const char* current)` in the header: the only
  logic (`strcmp(seen, current) != 0`), so an empty `seen` (first-ever boot) reads as "differs".
  Header is dependency-free and host-includable.
- `VersionMarker::currentVersionIsNew()` / `markCurrentVersionSeen()` in the `.cpp`, opening NVS
  (`Preferences`, namespace `"adversary"`, key `"lastSeenVer"`). The `<Preferences.h>` include
  and the NVS bodies are under `#ifdef ESP32`; the `#else` native path is a compile-only stub
  (`currentVersionIsNew` -> false, `markCurrentVersionSeen` -> no-op) with a WHY comment, exactly
  the `sd_manager.cpp` convention for a device-only capability that the native `+<core/>` glob
  still compiles. Native never calls these; the behaviour under test is `versionDiffers`.

**Screen** (`src/ui/screens/whats_new_screen.{h,cpp}`, `ScreenId::WHATS_NEW`): mirrors
`AboutScreen` (static line array + scroll offset + N/total indicator, ESC -> menu). It reads
`WHATS_NEW_LINES`; heading shows `VERSION`. No new scroll/render mechanism (ADR-0121 rule-of-three
note: second scrollable-static-text screen, so copy the shape, do not extract yet). Registered as
a static instance in `screen_registry.cpp`.

**Wiring (both triggers):**
- Boot auto-show, in `main.cpp` after `initializeMenu()` and after the existing no-SD toast: if
  `VersionMarker::currentVersionIsNew()`, `navigateToScreen(ScreenId::WHATS_NEW)` then
  `VersionMarker::markCurrentVersionSeen()`. Ordered after the toast so the no-SD advisory is not
  skipped; the write-on-trigger (not on-dismiss) keeps it idempotent across a boot-time power cut.
- Settings entry: a `SettingItem::action("What's New")` row in the Settings "System"/About area,
  routed through a new `onWhatsNewRequested_` callback (setter + dispatch in the action handler),
  wired in `screen_registry.cpp` to `setActiveScreen(ScreenId::WHATS_NEW)`: the exact pattern the
  "Saved Networks" row already uses.

## Verification

- `pio test -e native -f test_version_marker`: new. Covers `versionDiffers`: equal -> false;
  different -> true; empty `seen` (first boot) -> true; a prefix like "0.6.0" vs "0.6.0-alpha" ->
  true. This is the only branching logic; the NVS read/write is device-only (stubbed natively) and
  on-device verified below, matching the slice-0119 split (pure helper host-tested, persistence
  not separately round-tripped).
- `pio test -e native`: full suite stays green.
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS (serial; -j 2 avoids the
  host OOM). Record the flash/RAM delta to confirm ADR-0121's "negligible cost" claim.
- On-device: a ledger verify line tracks confirming (a) a flash/update shows the screen once at
  boot and the next reboot does not, (b) Settings > What's New reopens it, (c) ESC returns to menu,
  and (d) it still fires once with no SD card inserted.

## As built

Built as designed, all four pieces.

- `src/config/whats_new.h`: `WHATS_NEW_LINES[]` (header-inline `constexpr`) + `WHATS_NEW_LINE_COUNT`,
  curated for v0.6.0 "Field usability" (screen sleep, local-time offset, SD-aware greying, this
  screen), version-agnostic body since the heading shows `VERSION` live.
- `src/core/version_marker.{h,cpp}`: `versionDiffers()` inline/pure in the header;
  `VersionMarker::currentVersionIsNew()` / `markCurrentVersionSeen()` in the `.cpp` over
  `Preferences` (NVS namespace `"adversary"`, key `"lastSeenVer"`), with native compile-only
  stubs under `#else`.
- `src/ui/screens/whats_new_screen.{h,cpp}`, `ScreenId::WHATS_NEW`: the AboutScreen scrollable
  pattern (static lines + scroll offset + N/total, ESC -> menu), heading
  `WHAT'S NEW` with `VERSION` as the right subtitle.
- Wiring: registered in `screen_registry.cpp`; Settings gains a "What's New" action row
  (`onWhatsNewRequested_` callback, the Saved Networks pattern); `main.cpp` auto-shows it once at
  boot after a version change, advancing the NVS marker at trigger time.
- CLAUDE.md release checklist gains the "rewrite whats_new.h" obligation (step 3).

Verification run this change:
- `pio test -e native -f test_version_marker`: 4/4 pass (equal, different, empty-first-boot, prefix).
- `pio test -e native`: 920/920 cases pass (was 916; +4 new).
- `pio run -e cardputer -j 2`: SUCCESS (RAM 26.5%, Flash 75.5%, +0.1% over slice-0120's 75.4%).
- `pio run -e m5stick -j 2`: SUCCESS (RAM 21.0%, Flash 78.4%, +0.1% over 78.3%). The +0.1% on both
  confirms ADR-0121's "negligible cost".
- On-device (2026-10-25): confirmed. The screen auto-shows once at boot after the update and the
  next reboot goes straight to menu; Settings > What's New reopens it; ESC returns to menu; it
  still fires once with no SD card seated.
