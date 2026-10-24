---
id: '0120'
title: "Grey SD-dependent Settings actions when no card is present"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

Close the residual of the slice-0020 follow-up: a Settings action whose entire purpose is SD
I/O must show as unavailable (greyed, unselectable) when no card is mounted, so the operator
sees it before triggering it, not only after a failure.

The measured residual is a single action. The root menu already greys the SD-backed entries an
operator hunts for (Captures, BadBLE, Server) live via `MenuItem::gated`/`enabledFn` (slice-0042);
the "~34 SD-touching screens" in the original ledger line was an over-count of `modules/`+`hal/`
call sites, not operator entry points. Inside Settings, only **Rebuild Index** is a pure SD
operation, and today with no card it calls `CaptureRegistry::rebuildIndex()` on an unmounted card
and reports **`"Index rebuilt: 0"` as a SUCCESS toast** (`settings_screen.cpp`): a fallback that
masks a real failure, which CLAUDE.md "fail loud" forbids. Greying the row is the fix and, because
the confirm dialog only opens when the row is selected, the single complete gate.

## Definition of Done

- **Given** no SD card is mounted
- **When** the operator opens Settings and reaches the Storage section
- **Then** "Rebuild Index" renders greyed with a right-aligned "(No SD)" marker and the cursor
  cannot land on it (skip-nav steps over it to "Retry SD Mount"), so the false-success rebuild is
  unreachable.

- **Given** no SD card is mounted and the cursor is above the Storage section
- **When** the operator navigates down through it
- **Then** the selection skips the greyed "Rebuild Index" and lands on "Retry SD Mount", which
  stays selectable (it is the recovery action and must never be gated).

- **Given** an SD card is mounted
- **When** the operator opens Settings
- **Then** "Rebuild Index" is selectable and behaves exactly as before: no regression to the
  confirm dialog or the deferred rebuild.

- **Given** a cardless boot, with "Rebuild Index" greyed
- **When** the operator selects "Retry SD Mount" and a card mounts
- **Then** "Rebuild Index" un-greys, because the Retry handler already calls `buildSettingsList()`,
  which re-reads the live gate.

## Design

A live gate on `SettingItem`, the third instance of the slice-0042 pattern (`MenuItem::gated`
then carousel `enabledFn`), so by the rule of three it is generalised rather than copied a third
time:

- `std::function<bool()> enabledFn` (empty = always enabled) and `const char* disabledHint`
  (the right-aligned reason shown when gated-off, e.g. "No SD"), with
  `bool isEnabled() const { return !enabledFn || enabledFn(); }` mirroring `MenuItem::isEnabled`.
  Factory `SettingItem::gatedAction(label, disabledHint, enabledFn)` builds an ACTION row carrying
  both. The header gains `#include <functional>`.
- Selectability becomes `isSelectableRow(type) && item.isEnabled()` at every site that enumerates
  selectable rows: the `ListScrollModel` predicate (so the cursor skips a gated-off row the same
  way it skips a header), the `selectableCount_` tally, and the scrollbar position scan. The three
  share the combined test, so a gated-off row is uniformly invisible to navigation.
- `drawSettingRow` ACTION/INFO case: when `!isEnabled()`, draw the label in `TEXT_DISABLED` and a
  right-aligned "(hint)" in `TEXT_DISABLED` (reusing the NUMBER value slot idiom). INFO carries no
  `enabledFn`, so it is always enabled and its render is unchanged.

Applied to exactly one row:
`SettingItem::gatedAction("Rebuild Index", "No SD", []{ return SDManager::getInstance().isReady(); })`,
the same `isReady()` bool the menu gates on (a flag read, safe to poll per frame).

Deliberately **not** gated, boundary recorded so it is a decision, not an omission:
- **Retry SD Mount** is the recovery path; gating it on SD presence would be a deadlock.
- **API-key rows** (WPA-SEC / WiGLE / pwncrack) and **Saved Networks** set in-memory values that
  work for the session and produce no false success; their SD dependency is persistence, which is
  orthogonal to this item (the whole settings struct persists only with a card, and that is
  communicated once per slice-0020, not per row). Greying them would block a legitimate bench
  setup with no card seated.

No new test: the added logic is the one-line boolean `isEnabled()` living in a UI header that is
not host-compilable (it pulls the M5Unified draw stack), exactly like the slice-0042
`MenuItem::isEnabled` it mirrors, which is likewise verified on-device. A trivial `!fn || fn()`
test would require decoupling `SettingItem` from the UI header for no real coverage. Verification
is the native/device builds plus the on-device check below.

## Verification

- `pio test -e native`: stays green (the `SettingItem` struct change compiles; existing suites
  unaffected).
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS (serial; -j 2 avoids the
  host OOM).
- On-device: a ledger verify line tracks confirming that with no card "Rebuild Index" is greyed
  with "(No SD)" and unselectable while "Retry SD Mount" stays selectable, and that after a Retry
  SD Mount with a card inserted the row un-greys and rebuilds normally.

## As built

Built as designed, a single gated row.

- `SettingItem` gained `std::function<bool()> enabledFn` + `const char* disabledHint`,
  `isEnabled()` (`!enabledFn || enabledFn()`, mirroring `MenuItem::isEnabled`), and the
  `gatedAction(label, disabledHint, enabledFn)` factory; the header now includes `<functional>`.
- `settings_screen.cpp`: the "Rebuild Index" row is now
  `gatedAction("Rebuild Index", "No SD", []{ return SDManager::getInstance().isReady(); })`. A
  file-local `isSelectableItem(const SettingItem&)` folds `isSelectableRow(type) && isEnabled()`
  and replaced the bare `isSelectableRow(type)` at all three enumeration sites (the
  `ListScrollModel` predicate, the `selectableCount_` tally, the scrollbar position scan), so a
  gated-off row is skipped uniformly. `drawSettingRow` greys a gated-off ACTION and draws its
  `(No SD)` hint right-aligned in the value slot.
- No handler guard added: the confirm dialog opens only on selecting the row, so greying it is the
  single complete gate to the false-success rebuild. A separate guard would be unreachable
  (dead code).

Verification run this change:
- `pio test -e native`: 916/916 cases pass.
- `pio run -e cardputer -j 2`: SUCCESS (Flash 75.4%, RAM 26.5%).
- `pio run -e m5stick -j 2`: SUCCESS (Flash 78.3%, RAM 21.0%).
- On-device (Cardputer, 2026-10-24): confirmed. With no card, "Rebuild Index" shows greyed with
  "(No SD)" and the cursor skips it to "Retry SD Mount"; after a Retry SD Mount with a card
  inserted, the row un-greys and rebuilds normally.
