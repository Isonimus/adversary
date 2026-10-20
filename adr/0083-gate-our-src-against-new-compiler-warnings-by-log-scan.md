---
id: '0083'
title: "Gate our own src/ against new compiler warnings with a build-log scanner, not -Werror"
type: architecture
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Context

Slices 0081 and 0082 brought our own `src/` to **zero** compiler warnings on both device
builds (vendored in-`src/` trees excluded): 0081 cleared the seven low-risk `-Wall`/`-Wextra`
classes, 0082 migrated the `-Wdeprecated-declarations` set. `-Wall -Wextra` are on in every
embedded env, but they are non-fatal, so nothing stops a new warning from creeping back in.
The warning-cleanup arc always had a final step: a ratchet that keeps the clean state clean.

The ledger recorded that ratchet as "`-Werror` scoped to our `src/`, with in-source
suppressions for the handful of vendored-header false positives that leak through our TUs
(NimBLE `nimconfig.h`, framework `USBHIDKeyboard.h`)." Measured against the real build on
2026-10-20, that mechanism does not work:

- **`build_src_flags = -Werror` fails the cardputer build with 20 errors.** PlatformIO's
  `build_src_flags` scopes flags to `src_dir` compilation (not `lib/`, not the framework), so
  it is the correct *scope* knob. But our BLE translation units (`ble_scanner.cpp`,
  `ble_spanner.cpp`, `ble_utils.cpp`, `main.cpp`) `#include` NimBLE, which pulls `nimconfig.h`,
  and `nimconfig.h` **redefines** `CONFIG_BT_NIMBLE_ROLE_CENTRAL`, `..._ACL_BUF_COUNT`, and
  ~13 other `CONFIG_BT_NIMBLE_*` macros that the framework's own `sdkconfig.h` already
  defines. `-Werror` turns each redefinition into
  `error: "CONFIG_BT_NIMBLE_ROLE_CENTRAL" redefined [-Werror]`. The warning fires while
  compiling *our* TU (the pragma/flag context is our `-Werror`), even though the offending
  line is in a vendored header.
- **The redefinition warning is unflagged, so it cannot be suppressed.** It is tagged bare
  `[-Werror]`, not `[-Werror=<class>]`: GCC's preprocessor macro-redefinition diagnostic has
  no `-W` option name. There is therefore **no `-Wno-error=<class>`** to exempt it and **no
  `#pragma GCC diagnostic ignored "-W..."`** that can target it. The ledger's "in-source
  suppressions for the vendored false positives" is not achievable for this class: there is no
  flag to name. The only ways to silence it under `-Werror` are (a) stop including the
  conflicting headers (impossible: BLE needs NimBLE), (b) `#undef` ~15 version-coupled macros
  before every NimBLE include (fragile, breaks on any NimBLE/IDF bump), or (c) mark the
  vendored include directories `-isystem` so their warnings vanish (requires an `extra_scripts`
  hook that rewrites PlatformIO's `CPPPATH`, couples to PlatformIO internals, and also blinds
  us to any genuinely useful warning from those headers).

Meanwhile the repo already enforces four code-quality gates, and every one of them is a
**path-scoped scanner run in CI**, not a compiler flag: cppcheck (slice-0011), serial-escapes
(slice-0037), dep-lock-staleness (slice-0080), and raw-serial (ADR-0051 / slice-0052). cppcheck
in particular already gates "our `src/` only" simply by pointing the tool at `src/`
(`-I src src/`); vendored `lib/` is out of scope *by path*, not by any flag. The CI matrix also
already tees each build to `build.log` for the per-target size report (slice-0039), so a
compiled warning list is already produced on every run at no extra cost.

## Decision

Gate our own `src/` against **new** compiler warnings with a **build-log scanner scoped by
path**, not with `-Werror`.

- A new zero-dependency script, `scripts/lint-build-warnings.mjs`, parses a captured
  PlatformIO build log, keeps the `warning:` lines whose source path is under `src/` and is
  **not** in the vendored in-`src/` subtrees (`src/common/`, `src/fl/`, `src/lgfx/`,
  `src/class/`, `src/platforms/`, `src/third_party/`, and the vendored file `src/M5Unified.cpp`),
  and exits non-zero if any remain, printing each `file:line:col`. It follows the same
  zero-dependency, self-tested shape as the other four `scripts/lint-*.mjs` gates.
- It **reuses the `build.log`** the CI matrix already produces for the size report, run as a
  step after each build leg. CI does not cache `.pio/build`, so every CI build is cold and
  every our-`src/` warning is present in the log; the gate is authoritative there.
- Vendored-header warnings (the `nimconfig.h` macro-redefinitions, the `tusb_types.h`
  `-Wextra` conditional-expression warnings reached via `USBHIDKeyboard.h`, and any future
  vendored noise) stay out of scope **by path**, the same principle cppcheck's `src/`-only
  scope and raw-serial's file set already use. This sidesteps the unsuppressable
  macro-redefinition entirely: we never try to make the compiler ignore a vendored warning, we
  just do not attribute it to us.

Why not `-Werror`: measured infeasible above (unsuppressable vendored macro-redefinitions in
our BLE TUs). Why not the `-isystem` rewrite that would make `-Werror` feasible: it adds build
machinery coupled to PlatformIO's `CPPPATH` internals and blinds us to real warnings from the
vendored headers, for no enforcement gain over the log scan, which already turns any our-`src/`
warning into a red CI leg. KISS (global quality bar) and idiom-consistency with the four
existing gates settle it toward the scanner.

## Consequences

- **Our `src/` stays warning-clean under enforcement.** Any new warning in our own code turns
  the CI leg red, with the exact `file:line` in the log. The clean state 0081+0082 reached is
  now defended, which was the whole point of the arc.
- **Consistent with the repo's gate idiom.** It is the fifth path-scoped, self-tested CI
  scanner, not a new kind of mechanism, so it reads and maintains like the others. No
  `platformio.ini` change, no shipping-build risk (the device envs keep `build_unflags =
  -Werror=all` exactly as today), no vendored-header surgery.
- **Enforcement is "build then scan," not compile-fatal.** A warning still compiles; it is
  caught by the CI scan rather than by the compiler stopping the TU. The outcome is identical
  (a warning cannot merge), but a developer sees it as a failed gate, not a failed build.
- **Local runs are only as complete as the last build.** `.pio/build` is incremental locally,
  so a local scan sees warnings only from TUs that recompiled. The gate is authoritative in CI,
  where the build is cold; locally, run it after a clean build. This matches how cppcheck and
  the size report already behave.
- **The gate trusts GCC's path attribution in the log**, as the size step already trusts the
  `Flash:` line (slice-0039). A missing or empty log is treated fail-loud (the script errors
  rather than passing green). A deeper log-format drift is bounded by the gate running only on
  a completed build (CI step order), and remains a known residual, as it is for the size step.
- **Not a dead end for `-Werror`.** If the `-isystem` rewrite (or a NimBLE/IDF bump that fixes
  the `nimconfig.h`/`sdkconfig.h` redefinition) later removes the vendored noise from our TUs,
  a compile-fatal `-Werror` gate becomes feasible and could supersede this ADR. Until then, the
  log scan is the mechanism that actually works.
