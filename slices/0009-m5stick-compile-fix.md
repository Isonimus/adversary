---
id: '0009'
title: "Make the m5stick target compile again"
type: slice
status: accepted
date: 2026-09-06
supersedes: []
superseded_by: []
---

## Goal

`pio run -e m5stick` does not compile. The M5StickC Plus2 target had bit-rotted to
un-compilable and stayed that way because nothing builds it (no CI gate). Three real
blockers, each a case of code compiling for m5stick that references hardware the StickC
does not have:

1. **`sd_manager.cpp`** unconditionally references `pins::LORA_NSS`, `pins::CC1101_CS`, and
   `pins::NRF24_CS` (the shared-bus cap chip-select de-assert from slice-0002). `pins.h`
   defines those three only under `TARGET_CARDPUTER`; the `TARGET_M5STICK` and generic
   branches omit them (the StickC has no expansion header and no built-in SD). The block
   sits *after* the `TARGET_M5STICK` early return in `SDManager::init()`, so it is already
   dead code on the StickC — it just still gets compiled.
2. **`main.cpp`** has a `#elif defined(TARGET_M5STICK)` input branch that uses `Input()` and
   `InputAction` unqualified. Both live in `namespace adversary`, and `main.cpp` does not
   `using namespace adversary` (its Cardputer branch qualifies `adversary::` inline). That
   branch has never been compiled, so the missing qualification was never caught.
3. **`gps_config.h` / `gps_manager.cpp`** reference `pins::CAP_GPS_RX` / `pins::CAP_GPS_TX`
   (the "Cap" GPS pin set), which the m5stick branch of `pins.h` omits. The GNSS cap is
   exclusive to the Cardputer expansion header (Cardputer ADV); the StickC has no such
   header and uses Grove GPS only. The generic/native branch already defines these as `-1`
   so the shared GPS probe compiles and skips the absent pin set at runtime — the m5stick
   branch was simply incomplete.

**Scope note:** the LEDGER recorded only blockers 1–2 because the reproduction that produced
it stopped at the first failing translation unit. Blocker 3 surfaced only once 1–2 were
fixed and the build got further. The cap-radio HAL (`cc1101.cpp`, `nrf24.cpp`,
`expansion_cap.cpp`, `ook_rmt.cpp`) was *not* a blocker: those files already apply the
Cardputer-only body-guard + `#else`-stub pattern correctly, so their pin references compile
only under `TARGET_CARDPUTER`.

This slice makes the StickC target build again with the smallest correct change to each
blocker, so it can (next slice) join a CI matrix instead of silently rotting. The missing
build gate is the real reason this rot went unnoticed — that is the CI ledger item, and
this slice is its prerequisite.

## Definition of Done

- **Given** the blockers above
- **When** `pio run -e m5stick` is run
- **Then** it compiles and links with no errors (a genuine link/build, exit 0)

- **Given** the fixes are applied
- **When** `pio run -e cardputer` is run
- **Then** it still compiles and links (the guard and the qualification must not regress the
  Cardputer path — its cap-CS de-assert must remain intact, since that is the target where
  it actually matters)

- **Given** the fixes are applied
- **When** `pio test -e native` is run
- **Then** all existing native tests still pass (no source shared with native regressed)

## Design

### Blocker 1 — sd_manager.cpp cap-CS de-assert

Guard the four cap-CS lines (and their explanatory comment) under
`#if defined(TARGET_CARDPUTER)`. This is not a workaround — it states in the preprocessor
what is already true at runtime: those pins exist only on the Cardputer expansion header,
the de-assert only matters there, and the StickC returns from `init()` before reaching them
anyway. The generic (`#else`) build in `pins.h` also lacks these pins, so a
`TARGET_CARDPUTER` guard (not `!TARGET_M5STICK`) is the correct predicate — it enables the
block only where the pins are defined and meaningful.

### Blocker 2 — main.cpp m5stick input branch

Pull the two names into the branch with scoped `using adversary::Input;` and
`using adversary::InputAction;` at its top. This matches the file's own idiom for reaching
into the namespace (`showMenu` uses scoped `using adversary::MenuItem;` declarations) and
keeps the branch body — a switch with eleven `InputAction`/`Input` references — readable,
rather than repeating `adversary::` at every site. The menu handlers the branch calls —
`Menu::handleAction(InputAction)` and `CarouselMenu::handleAction(InputAction)` — already
exist, so once the names resolve the branch type-checks with no further change.

### Blocker 3 — GPS Cap pin set on m5stick

Add `CAP_GPS_RX = -1` and `CAP_GPS_TX = -1` to the `TARGET_M5STICK` branch of `pins.h`,
mirroring the generic/native branch verbatim. This is the root-cause fix, not a guard: the
GPS module is deliberately cross-target (its probe logic is native-unit-tested), so the
codebase's convention for "this pin set is absent on this board" is the `-1` sentinel plus
the runtime `rxPin < 0` skip in `gps_manager.cpp`/`GPS_PIN_SETS`, *not* a preprocessor guard
around the array entry. The m5stick branch simply had an incomplete pin table. With the pins
defined `-1`, the "Cap" GPS pin set is present-but-absent and skipped at runtime, leaving the
Grove pin set (G33/G32) as the StickC's GPS path.

This split — `-1` sentinel for native-testable code vs. `#if TARGET_CARDPUTER` body-guard +
`#else` stubs for pure hardware I/O (the cap radios) — is a coherent, intentional convention,
not an antipattern: sentinel pins let unit-tested logic compile natively; preprocessor guards
keep untestable radio drivers out of non-cap builds while still linking via no-op stubs.

### Why no native unit test

Both fixes are compile-time only. sd_manager's `init()` body is entirely under
`#ifndef UNIT_TEST`, so native never compiles it; the m5stick input branch is device input
handling with no host seam. There is no logic to exercise natively — the observable
behaviour is "the target compiles". The regression guard is therefore the build itself for
all three envs (recorded in `## As built`), which the very next slice (CI) makes permanent
and automatic. Fabricating a native test here would assert nothing about the thing that
broke.

## Verification

- `pio run -e m5stick` — compiles and links clean (was failing; the acceptance criterion).
- `pio run -e cardputer` — still compiles and links; cap-CS de-assert unchanged on the
  target where it matters.
- `pio test -e native` — full suite still green.
- Recorded in `## As built`.

## As built

Shipped essentially as designed, with the scope correction above: three fixes, not two.

- **`sd_manager.cpp`** — wrapped the four cap-CS de-assert lines in
  `#if defined(TARGET_CARDPUTER)` (with a comment noting the pins are expansion-header-only
  and the block is already dead on the StickC via the early return). Matches the identical,
  already-guarded block in `main.cpp` `setup()`.
- **`main.cpp`** — added scoped `using adversary::Input;` / `using adversary::InputAction;`
  at the top of the `TARGET_M5STICK` input branch (the `showMenu` idiom).
- **`pins.h`** — added `CAP_GPS_RX = -1` / `CAP_GPS_TX = -1` to the `TARGET_M5STICK` branch,
  mirroring the generic/native branch.

No cap-radio HAL file needed changing — `cc1101.cpp`, `nrf24.cpp`, `expansion_cap.cpp`, and
`ook_rmt.cpp` already guard their bodies under `TARGET_CARDPUTER` with `#else` no-op stubs,
so `radio_screen.cpp` (compiled cross-target) links against the stubs on m5stick.

**Builds (all three envs green):**
- `pio run -e m5stick` — **SUCCESS** (was failing). RAM 20.8 % (68,312 B), Flash 75.3 %
  (2,369,299 B of the `huge_app` 3 MB partition). This is the first successful StickC build
  since the multi-radio cap subsystem landed.
- `pio run -e cardputer` — **SUCCESS**, RAM 26.3 % (86,232 B) / Flash 71.7 % (2,395,999 B) —
  byte-for-byte identical to the slice-0008 figures, confirming no regression on the target
  where the cap-CS guard and the cap pins are live.
- `pio test -e native` — **698/698**, unaffected by the m5stick-branch-only `pins.h` change.

**Not verified on hardware.** This is a compile/link fix for a secondary target; no M5StickC
Plus2 device was flashed. The acceptance criterion is a clean cross-target build, which the
next slice (CI) makes a permanent, automatic gate — the true fix for the silent rot that let
this target break unnoticed.
