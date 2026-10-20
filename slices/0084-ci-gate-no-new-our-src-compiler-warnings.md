---
id: '0084'
title: "CI gate: no new compiler warnings in our own src/"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

Deliver the warning-cleanup arc's final step: a CI gate that keeps our own `src/`
warning-clean, so the zero-warning state slices 0081 and 0082 reached cannot silently regress.
This closes the ledger `[feature]` that tracked the "`-Werror` ratchet scoped to our `src/`".

That ledger item assumed the mechanism would be `-Werror`. ADR-0083 records why it is not:
measured 2026-10-20, `build_src_flags = -Werror` fails the cardputer build on 20 unsuppressable
vendored macro-redefinitions (`nimconfig.h` redefines `CONFIG_BT_NIMBLE_*` that the framework
`sdkconfig.h` already defines; the warning is unflagged, so no `-Wno-error=` and no `#pragma`
can target it). Per ADR-0083 the gate is instead a **path-scoped build-log scanner**, the fifth
in the family of `scripts/lint-*.mjs` CI gates (cppcheck, serial-escapes, dep-lock, raw-serial).

## Definition of Done

- **Given** a cardputer `build.log` from a build where our own `src/` is warning-clean (it is,
  after 0081+0082) but vendored headers still warn (the `nimconfig.h` macro-redefinitions and
  the `tusb_types.h` `-Wextra` conditional-expression warnings are always present)
- **When** `node scripts/lint-build-warnings.mjs build.log` runs
- **Then** it exits 0: the vendored warnings are excluded by path and no our-`src/` warning
  remains.

- **Given** a build log that contains a warning on an our-`src/` path (e.g.
  `src/modules/foo/bar.cpp:12:5: warning: unused variable 'x' [-Wunused-variable]`)
- **When** the scanner runs on it
- **Then** it exits 1 and prints that `src/modules/foo/bar.cpp:12:5` line, so the offending site
  is named.

- **Given** a build log that contains a warning on a vendored path, whether an in-`src/`
  vendored tree (`src/lgfx/...`) or an out-of-`src/` vendored path (`.pio/libdeps/.../nimconfig.h`,
  `/.../.platformio/.../tusb_types.h`)
- **When** the scanner runs on it
- **Then** it exits 0: vendored warnings are out of scope by path, exactly as cppcheck's
  `src/`-only scope excludes `lib/`.

- **Given** a missing or empty log path
- **When** the scanner runs
- **Then** it exits non-zero with a clear error (fail-loud), rather than passing green on no
  input.

- **Given** the self-test `scripts/lint-build-warnings.test.mjs`
- **When** `node --test scripts/lint-build-warnings.test.mjs` runs
- **Then** every case passes.

## Design

### What exists, and what this reuses

- **The scanner idiom is the four existing `scripts/lint-*.mjs` gates.** Each is a
  zero-dependency Node script exporting a pure, testable scan function plus a `main(args)` that
  returns an exit code, guarded by `if (import.meta.url === \`file://${process.argv[1]}\`)`, with
  a `node --test` self-test beside it. `lint-build-warnings.mjs` is written to that exact shape;
  the pure function is `scanLog(text)` returning the our-`src/` warning findings. This reuses the
  idiom deliberately rather than inventing a new gate style.
- **The path-scope principle is cppcheck's (slice-0011).** cppcheck gates "our `src/` only" by
  pointing at `src/` and letting `lib/` fall out of scope by path. The scanner does the same to
  the log: a `warning:` line is ours iff its path starts with `src/` and is not in a vendored
  in-`src/` subtree. The vendored exclusion set is the same one 0081/0082 used for their warning
  counts: `src/common/`, `src/fl/`, `src/lgfx/`, `src/class/`, `src/platforms/`,
  `src/third_party/`, and the vendored file `src/M5Unified.cpp`. Everything not under `src/`
  (`.pio/...`, the absolute `.platformio` framework path, `lib/...`) is vendored by construction.
- **The input is the `build.log` CI already tees (slice-0039).** The matrix build step runs
  `${{ matrix.cmd }} 2>&1 | tee build.log` for the size report; the gate reads that same file, so
  it adds no compile. CI does not cache `.pio/build`, so each run is a cold build and every
  our-`src/` warning is in the log.
- **Measured why-not-`-Werror`:** see ADR-0083. The probe (`PLATFORMIO_BUILD_SRC_FLAGS=-Werror
  pio run -e cardputer`, 2026-10-20) failed with 20 `nimconfig.h` macro-redefinition errors,
  tagged bare `[-Werror]` with no class name, hence unsuppressable. That measurement is this
  slice's reason for being a scanner, not a flag.

### The scanner

- `scanLog(text)` splits the log into lines and matches each against
  `^(.+?):(\d+)(?::(\d+))?: warning: (.*)$` (non-greedy path up to the first `:line[:col]:
  warning:`). It strips a leading `./`, then classifies the path: **ours** iff it starts with
  `src/` and does not start with any vendored prefix and is not the vendored `src/M5Unified.cpp`.
  Returns `{ path, line, col, message }` for the ours matches only.
- `main(argv)`: log path defaults to `build.log`. If the path does not exist or the file is
  empty, print an error and return 1 (fail-loud: the gate cannot certify a log it did not read).
  Otherwise scan; if any findings, print each `path:line:col: warning: message` and a one-line
  summary, return 1; else print a brief OK and return 0.

### CI wiring

Two steps are added to the existing matrix job (`.github/workflows/ci.yml`), after the build
step, for **every** leg (native, cardputer, m5stick), so each env's own compile is gated:

- **Self-test the warnings lint** - `node --test scripts/lint-build-warnings.test.mjs`. Run in
  the matrix (not a standalone job) because the scan it guards lives here; running it on each leg
  verifies the gate script before that leg uses it, at ~0.1 s cost.
- **Gate: no new our-src compiler warnings** - `node scripts/lint-build-warnings.mjs build.log`.

Placed in the matrix rather than as a standalone job (unlike the other four gates) because the
scan needs the compile artifact the matrix already produces; a standalone job would recompile
~150 s for nothing. This trade is recorded in ADR-0083.

### What is intentionally not done

No `-Werror`, no `platformio.ini` change, no `-isystem` `CPPPATH` rewrite, no NimBLE bump. The
device envs keep `build_unflags = -Werror=all` exactly as today. Widening to a compile-fatal
`-Werror` is left to a future slice if the vendored noise is ever removed (ADR-0083).

## Verification

- **Self-test:** `node --test scripts/lint-build-warnings.test.mjs` - all cases green. Covers
  each Definition-of-Done scenario at the `scanLog`/`main` level: an our-`src/` warning is
  flagged; an in-`src/` vendored warning and an out-of-`src/` vendored warning are both ignored;
  the real vendored lines (`nimconfig.h` redefinition, `tusb_types.h` `-Wextra`) are ignored;
  multiple findings all reported; missing/empty log fails loud.
- **Against the real build:** `node scripts/lint-build-warnings.mjs <cardputer build.log>` on a
  current clean build - exits 0 (our `src/` is warning-clean; the ~126 vendored warnings are
  excluded by path). Record the pass.
- **Negative control:** run the scanner on a fixture log carrying one synthetic our-`src/`
  warning line - exits 1 and names the site.
- **Doc gate:** `node scripts/build-index.mjs` + `node scripts/lint-docs.mjs` green with ADR-0083
  and this slice added.
- Device builds are unaffected (no `platformio.ini` change); a spot `pio run -e cardputer` stays
  `SUCCESS`.

## As built

All five Definition-of-Done scenarios were met. Shipped `scripts/lint-build-warnings.mjs`
(pure `scanLog` + `isOurSrcPath`, a `main` with a fail-loud missing/empty-log guard) and its
10-case self-test `scripts/lint-build-warnings.test.mjs`, and wired two steps into the CI
matrix job (`.github/workflows/ci.yml`): "Self-test the warnings lint" and "Gate: no new
warnings in our own src/", run on every leg after the build, reusing the leg's `build.log`.

- **Verification (all green, measured 2026-10-20).**
  - Self-test: `node --test scripts/lint-build-warnings.test.mjs` -> 10/10 pass.
  - Real clean build: the scanner on a current cardputer `build.log` -> exit 0 (our `src/` is
    warning-clean).
  - Exclusion on real noise: the scanner on the `-Werror` probe log that carried 29 real
    vendored warnings (`nimconfig.h` redefinitions + `tusb_types.h` `-Wextra`) -> 0 ours,
    exit 0. This is the stronger proof than the clean log: real vendored warnings present, all
    excluded by path.
  - Negative control: the same log with one synthetic our-`src/` warning appended
    (`src/modules/scanner/wifi_scanner.cpp:123:9: warning: ...`) -> exit 1, printing that exact
    site. The gate catches a regression and names it.
  - Fail-loud: a missing log path and an empty log each -> exit 1 with a clear message.
  - Doc gate: index regenerated to 82 document(s) (ADR-0083 + this slice added); `lint-docs`
    reports no error against either new doc. (The two `lint-docs` errors seen locally are a
    pre-existing working-tree transient unrelated to this slice: a gitignored `LEDGER.md` cites
    an untracked ADR a parallel session is still adding, so both are absent from the committed
    tree the pre-commit hook archives, and the hook runs green.)
  - Device build unaffected: `pio run -e cardputer` -> `SUCCESS` (no `platformio.ini` change).
- **The mechanism is the measured one, not the ledger's assumption.** The gate is a path-scoped
  log scanner because `-Werror` is infeasible here (ADR-0083): a probe
  (`PLATFORMIO_BUILD_SRC_FLAGS=-Werror pio run -e cardputer`) failed the build with 20
  `nimconfig.h` macro-redefinition errors tagged bare `[-Werror]`, which have no class name and
  so cannot be suppressed by `-Wno-error=` or `#pragma`.
- **No deviations from the plan** beyond adopting the scanner mechanism ADR-0083 settled: the
  scope set, the fail-loud guards, and the matrix wiring landed as designed.
- **Not done, by design.** No `-Werror`, no `platformio.ini` change, no `-isystem` `CPPPATH`
  rewrite, no NimBLE bump. A compile-fatal `-Werror` gate is left to a future slice if the
  vendored noise is ever removed from our TUs (ADR-0083 records the path).
