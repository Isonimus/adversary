---
id: '0095'
title: "Split handshake into Auto Hunt and Manual Capture screens"
type: slice
status: accepted
date: 2026-10-21
supersedes: []
superseded_by: []
---

## Goal

Implement ADR-0094: present handshake's two operations as peer menu entries and fix the
mode at launch instead of toggling it at runtime. Today Auto Hunt and Manual share one
screen behind a mutable `autoHuntMode_` bool reachable only by pressing `M`
(`handshake_screen.cpp:204`), which buries the Auto Hunt flagship, hides a surprising
ENTER-revert (`handshake_screen.cpp:228-231`), and, through its mode-varying selectable
count (2 vs 5, `handshake_screen.cpp:214-215`), both blocks the uniform-row convergence and
forces the ADR-0092 handshake exception.

This slice makes Auto Hunt and Manual Capture two entries under a new nested
`Wireless -> Handshake` submenu, backed by one `HandshakeScreen` launched in an immutable
mode via two `ScreenId`s. It is the prerequisite for the deferred `ConfigRow` slice (the
`LEDGER` line-91 feature): after the split, Manual is a fixed-count config form. This slice
does **not** itself restyle Manual's rows onto `ConfigRow`; that is the next slice.

## Definition of Done

- **Given** the root menu at `Wireless`
- **When** the operator opens `Handshake`
- **Then** a submenu lists `Auto Hunt` and `Manual Capture` as peers (plus `Back`), and the
  old single `Handshake Capture` leaf no longer exists

- **Given** the `Wireless -> Handshake` submenu
- **When** the operator selects `Auto Hunt`
- **Then** the screen opens directly in auto-hunt mode (the scan/lock/attack loop and its
  `START` are present), with no `M` toggle and no path to Manual from inside it

- **Given** the `Wireless -> Handshake` submenu
- **When** the operator selects `Manual Capture`
- **Then** the screen opens directly in manual mode showing Target + three option rows +
  `START`, and ENTER on the Target row opens the WiFi scanner, with the picked network
  returning as the target over the existing hand-off path

- **Given** either handshake screen
- **When** the operator presses `M`/`m`
- **Then** nothing toggles (the key and the mutable `autoHuntMode_` are gone) and no hidden
  ENTER-revert between modes exists

- **Given** either handshake screen in its CONFIG view
- **When** the operator presses Back/ESC
- **Then** it returns to the `Wireless -> Handshake` submenu, not the root menu

- **Given** a capture running in either mode
- **When** a handshake is captured and saved
- **Then** the CAPTURING and SUCCESS views and the session handshake list behave exactly as
  before the split, and `w` (whitelist-and-skip) and `h` (view list) stay Auto-Hunt-only

- **Given** the Manual config form
- **When** the operator presses ENTER on a value row
- **Then** the field advances per the ADR-0092 canonical model (handshake is no longer an
  interaction exception; settings remains the sole config-form exception)

## Design

Implements ADR-0094; read it for the topology decision and the rejected alternative.

**ScreenId.** Replace `ScreenId::HANDSHAKE` (`screen_interface.h:21`) with `HANDSHAKE_AUTO`
and `HANDSHAKE_MANUAL`. `ScreenId` is runtime navigation state only (never persisted to SD;
the `ATTACK_TARGET_SELECTED` payload carries it within a single session), so the enum change
is ordinal-safe. Update the five references: the two factories (`screen_registry.cpp`),
`getId()` (`handshake_screen.h:99`), the route table (`menu_routes.cpp:24`), and the two
target hand-offs, `scanner_screen.cpp:295` and `sniffer_actions.cpp:18`, both of which carry
a chosen network and therefore target `HANDSHAKE_MANUAL` (Auto Hunt finds its own targets).

**Mode.** Add `enum class HandshakeMode { AutoHunt, Manual }`; `HandshakeScreen` takes it in
its constructor and stores a `const HandshakeMode mode_`. Every current `autoHuntMode_` read
becomes `mode_ == HandshakeMode::AutoHunt`. Delete the `autoHuntMode_` member, the `M`/`m`
case, and the ENTER row-0 revert branch (`handshake_screen.cpp:204-205,228-231`). `getId()`
returns `mode_ == AutoHunt ? HANDSHAKE_AUTO : HANDSHAKE_MANUAL`. Default CONFIG selection is
set per mode at construction (Auto Hunt on `START`; Manual on the Target row, mirroring the
evil_twin first-step default from slice-0093) instead of by the old toggle.

**Factories.** Two registrations: `new HandshakeScreen(HandshakeMode::AutoHunt)` and
`...::Manual)`. This extends the one-ScreenId-per-factory shape every other attack screen
already uses (`screen_registry.cpp`) to two, rather than introducing a launch-intent
channel.

**Menu.** Add `ACTION_HANDSHAKE_AUTO` and `ACTION_HANDSHAKE_MANUAL` (`menu_actions.h`),
retiring `ACTION_HANDSHAKE_CAPTURE`. In `buildRootMenuItems()`, replace the Handshake leaf
in `wirelessMenu` (`menu_factory.cpp:23`) with
`MenuItem::submenu("Handshake", handshakeMenu, 'h')`, where `handshakeMenu` is
`{ action("Auto Hunt", ACTION_HANDSHAKE_AUTO, 'a'), action("Manual Capture",
ACTION_HANDSHAKE_MANUAL, 'm'), separator(), back() }`. Add two rows to `MENU_ROUTES`
(`menu_routes.cpp`), both `transitions=true, AppState::ATTACKING`, matching the retired row.

**What existing code was considered.** The nested submenu reuses `MenuItem::submenu` exactly
as `Wireless`/`BLE`/`RFID` already nest (`menu_factory.cpp:96-100`); the component's
`menuStack_`/`enterSubmenu` supports arbitrary nesting (`menu.h:5`, `menu.cpp:252-264`), and
Back pops one level (`menu.cpp:162-175`), so depth-2 and its return path need no new code.
The Target-row-to-scanner reuses the evil_twin row-0 navigation pattern and the
`ATTACK_TARGET_SELECTED -> setParams` return path shipped in slice-0093 (`main.cpp:173-187`,
`scanner_screen.cpp`), so no scanner or `main.cpp` change is needed. **Considered and
rejected** (recorded in ADR-0094): RFID's static one-shot launch-intent flag
(`rfid_screen.h:29`), because handshake's modes are co-equal peers rather than a primary
flow plus a sub-flow, and it would add a second mutable launch-intent global.

Native excludes `src/ui/` (`platformio.ini` `build_src_filter` `-<ui/>`), so these screen
`.cpp`/`.h` changes compile only under the device envs; the real compile gate is
`pio run -e cardputer` / `-e m5stick`.

## Verification

No native unit test covers screen construction, navigation, or `handleInput` (UI is excluded
from the native env and has no host harness), so this slice is verified by device build +
on-device operation, the bar slices 0091 and 0093 used:

- `pio run -e native` stays green (858/858; proves nothing regressed in the shared models).
- `pio run -e cardputer` and `pio run -e m5stick` compile clean, built serially (never
  concurrently, per CLAUDE.md), and pass the ADR-0083 warnings gate
  (`node scripts/lint-build-warnings.mjs <build.log>`).
- `node scripts/lint-docs.mjs` clean (this slice + ADR-0094 indexed).
- **On-device** (operator): each Definition-of-Done scenario: the submenu lists both modes;
  each entry opens straight into its mode; `M` does nothing and no ENTER-revert exists;
  Back returns to the Handshake submenu; a capture in each mode still reaches SUCCESS and the
  session list with `w`/`h` Auto-Hunt-only; ENTER advances a Manual value row.

## As built

Handshake's two operations now launch as peer entries from a nested `Wireless -> Handshake`
submenu (`menu_factory.cpp`), backed by one `HandshakeScreen` whose mode is fixed at
construction. The old single `Handshake Capture` leaf, the `autoHuntMode_` bool, the `M`
toggle, and the hidden ENTER row-0 revert are all gone.

- **Mode is immutable.** Added `enum class HandshakeMode { AutoHunt, Manual }`; the
  constructor takes it and stores `const HandshakeMode mode_`. The 18 former `autoHuntMode_`
  reads became `isAutoHunt()`. Two `ScreenId`s (`HANDSHAKE_AUTO`, `HANDSHAKE_MANUAL`,
  replacing `HANDSHAKE`) map to two factories that construct the screen in each mode;
  `getId()` returns the matching id. `ScreenId` is runtime-only, so the enum change was
  ordinal-safe.
- **Per-mode CONFIG.** Auto Hunt shows its info paragraph + `START` as the single selectable
  row (index 0); it no longer draws a Target row. Manual keeps Target (row 0) + three option
  rows + `START` (row 4). `configItemCount()` returns 1 vs `CONFIG_ITEMS` accordingly.
- **Canonical input (ADR-0092).** Manual value rows (1-3) gained `,`/`<` (previous) and
  `/`/`>`/ENTER (advance) through a new `handleConfigEdit(int delta)`; ENTER on the Target
  row routes to the scanner, on `START` it captures. Auto Hunt's ENTER starts. The handshake
  interaction exception in ADR-0092 is retired (settings is now the sole config-form
  exception).
- **Hand-offs.** `scanner_screen.cpp` and `sniffer_actions.cpp` publish to `HANDSHAKE_MANUAL`
  (Auto Hunt self-targets). The nested submenu uses the existing `MenuItem::submenu`; Back
  pops to the Handshake submenu, not root.
- **Copy + boyscout.** CONFIG headers now read "Auto Hunt" / "Manual Capture"; the dead
  "Mode - AUTO/MANUAL" footer hint was removed in favour of a single `Start`/`Select` hint.
  A redundant `if/else` in `drawConfig` that set the same text colour in both branches was
  collapsed.

Verification ran green: `pio test -e native` 858/858 (`test_sniffer_actions` updated to
assert the `HANDSHAKE_MANUAL` routing); `pio run -e cardputer` and `-e m5stick` both compiled
clean with the ADR-0083 warnings gate clean on each; `node scripts/lint-docs.mjs` clean. The
operator confirmed every Definition-of-Done scenario on-device 2026-10-21: the submenu lists
both modes, each opens straight into its mode, `M` does nothing and no revert exists, Back
returns to the Handshake submenu, captures in each mode still reach SUCCESS and the session
list with `w`/`h` Auto-Hunt-only, and ENTER advances a Manual value row.
