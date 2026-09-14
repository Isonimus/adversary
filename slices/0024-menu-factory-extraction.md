---
id: '0024'
title: "Extract the menu/carousel construction out of main.cpp into a menu factory"
type: slice
status: accepted
date: 2026-09-14
supersedes: []
superseded_by: []
---

## Goal

`main.cpp`'s `initializeMenu()` builds the entire navigation catalogue inline: the
hierarchical `MenuItem` tree (five submenus + Settings/About) and the root `CarouselItem`
tiles, greying entries per live hardware/SD detection. That is menu *data*, not entry-point
orchestration — the same "the entry point should not enumerate the catalogue" smell that
slice-0023 removed for the screen->factory table.

Move the item construction into a dedicated translation unit, `menu_factory.cpp`, exposed
through a one-header API (`buildRootMenuItems()` / `buildCarouselItems()`). After this,
`initializeMenu()` shrinks to wiring: set the built items on `mainMenu`/`carouselMenu` and
attach the `onAction` callbacks (which stay in `main.cpp` because they reference `main.cpp`
state — `mainMenu`, `showMenu()`, `handleMenuAction()`).

The `MenuActionId` enum moves too, into a shared `menu_actions.h`, because both the factory
(which builds items tagged with action ids) and the still-in-`main.cpp` `handleMenuAction()`
(which switches on them) need it.

Scope is deliberately narrow, and deliberately the **data** half of the ledger's paired
`MenuFactory`/`MenuController` item. `handleMenuAction()` — the *controller* — stays in
`main.cpp`: its Scanner/Sniffer cases do the concrete `static_cast<ScannerScreen*>` /
`static_cast<SnifferScreen*>` downcasts that slice-0023 left for the `IAttack`
polymorphic-lifecycle item. Extracting it now would relocate those five screen includes into
a new TU rather than resolve them, forcing a second rewrite when `IAttack` lands. So the
controller is deferred to bundle with that item.

## Definition of Done

- **Given** the firmware builds for `cardputer`
- **When** `main.cpp` is compiled
- **Then** `initializeMenu()` contains **zero** `MenuItem::`/`CarouselItem` construction — it
  only sets the factory's returned items on `mainMenu`/`carouselMenu` and attaches the
  `onAction` callbacks; the `MenuActionId` enum no longer lives in `main.cpp`

- **Given** the menu factory
- **When** `buildRootMenuItems()` and `buildCarouselItems()` run
- **Then** they produce the identical menu tree and carousel tiles the old inline block did —
  same labels, action ids, shortcut keys, separators, order, and the same greying/enabledFn
  gates keyed off the same detection state (`SDManager::getInstance().isReady()`,
  `ui::g_gpsDetected`, `ui::g_rfidDetected`, `hal::resolvedExpansionCap()`)

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** the suite is green and the factory TU is **not** compiled (it lives under `ui/`,
  which the native `build_src_filter` excludes, and pulls `Arduino.h`/`M5Unified.h` via
  `carousel_menu.h` regardless)

## Design

### What moves and what stays

Moves to `menu_factory.cpp`: the verbatim construction of the five submenu `MenuItem`
vectors (Wireless/BLE/Infrared/RFID/HID), the root menu vector, and the `CarouselItem`
vector — including every greying ternary and the live `enabledFn` gates. The factory reads
the detection state directly from the same singletons/externs `initializeMenu()` reads today
(`SDManager::getInstance()`, `ui::g_gpsDetected`, `ui::g_rfidDetected`,
`hal::resolvedExpansionCap()`), so no state is threaded through parameters — this is a
relocation, not a signature redesign.

Stays in `main.cpp`: the *wiring*. `initializeMenu()` becomes set-title + set-items +
set-onAction. The `onAction` callbacks cannot move — the `mainMenu` `onAction` calls
`handleMenuAction()`, and the carousel `onAction` also manipulates `mainMenu` and calls
`showMenu()` for the negative-actionId (submenu-jump) path. All three are `main.cpp` symbols.

### The action-id enum: relocated, not namespaced

`MenuActionId` is today an **unscoped, global-namespace** enum in `main.cpp`. Only its
enumerators are ever used, always implicitly as `int` (`handleMenuAction(int)` switches on
them; `MenuItem::action(...)` takes them as `int`) — the type name `MenuActionId` is never
spelled as a parameter or variable type. It therefore relocates to `menu_actions.h` as an
exact drop-in: keep it global-scope and unscoped, and both `main.cpp` and `menu_factory.cpp`
`#include` it.

Wrapping it in `namespace adversary` or making it an `enum class` is deliberately **out of
scope**: it would touch all ~30 `case` labels in `handleMenuAction()` and every
`MenuItem::action()` call site, turning a clean relocation into a semantic edit that buries
the move in noise. That tidy-up, if wanted, is its own trivial follow-up.

### Why the factory is not native-testable (and that is fine)

A pure factory over plain detection booleans would be an obvious native-test target — assert
the tree given each flag combination. But `carousel_menu.h` pulls `Arduino.h` + `M5Unified.h`
and the tiles reference `assets::ICON_*` (embedded BMP blobs), so the TU only compiles for
the embedded targets and the native `build_src_filter` excludes `ui/` anyway. Like
slice-0023, this is a relocation of verbatim construction with no new pure logic, so the
verification is diff-equivalence + build + on-device, not a unit test. (Making the menu tree
native-testable would mean separating the tile *model* from its Arduino/asset rendering — a
larger design change, not this slice.)

## Verification

- Native: `pio test -e native` full suite green (no native source changed; factory TU
  excluded by `-<ui/>`).
- Build: `pio run -e cardputer` compiles and links — the real arbiter, since the factory TU
  is compiled only for embedded targets.
- Menu-tree equivalence: the moved construction is byte-for-byte the same as the old inline
  block (a relocation, reviewed in the diff) — same labels/action-ids/shortcuts/separators/
  order and the same greying gates.
- On-device (operator): open every submenu (Wireless/BLE/Infrared/RFID/HID) and confirm all
  entries present in the same order; confirm the greyed entries grey correctly for the
  current SD/GPS/RFID/cap state; confirm the carousel tiles and their submenu-jump navigation
  still work.

## As-built

Shipped as designed. New `src/ui/menu_actions.h` (the relocated global-scope `MenuActionId`
enum, verbatim), `src/ui/menu_factory.{h,cpp}` (`buildRootMenuItems()` /
`buildCarouselItems()` holding the item construction verbatim from the old `initializeMenu()`
block, reading the same detection singletons/externs). `main.cpp` dropped the enum and the
~130-line construction: `initializeMenu()` is now ~20 lines — set-title + set-items(factory)
+ set-onAction for both `mainMenu` and `carouselMenu`; the `onAction` callbacks stayed (they
reference `handleMenuAction()`, `mainMenu`, `showMenu()`). Two includes added
(`ui/menu_actions.h`, `ui/menu_factory.h`). `handleMenuAction()` untouched — deferred to the
`MenuController`/`IAttack` bundle.

Verification:
- `pio run -e cardputer` SUCCESS — compiles, links, packages a valid ESP32S3 image. RAM 26.4%
  (86,632 B, unchanged), Flash 73.9% (2,468,995 B; +204 B vs. HEAD — the call-boundary
  indirection, negligible).
- `pio test -e native` 752/752 (factory/menu-actions TUs excluded from the native build by
  `-<ui/>`; no native source changed).
- Catalogue equivalence: the 63 item-construction lines (labels/action-ids/shortcuts/
  separators/push_backs) diffed against `HEAD` — **identical**, confirming a verbatim move.
- On-device (operator, 2026-09-14): confirmed — every submenu (Wireless/BLE/Infrared/RFID/
  HID) shows the same entries in the same order with the same greying, and the carousel tiles
  + submenu-jump navigation work. (A separate, pre-existing observation surfaced: submenus
  render 4 rows and leave a ~15px gap — `maxVisible = 87/18 = 4` in menu.h, unchanged since the
  initial release and untouched by this slice. Tracked as its own UX follow-up, not part of
  this relocation.)
