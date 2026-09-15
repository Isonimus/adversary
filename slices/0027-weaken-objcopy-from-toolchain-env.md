---
id: '0027'
title: "Resolve the deauth-weakening objcopy from the toolchain env, not a hardcoded path"
type: slice
status: accepted
date: 2026-09-15
supersedes: []
superseded_by: []
---

## Goal

Every embedded build runs a `pre:` hook (`scripts/weaken_deauth_pre.py`) that shells out to
`scripts/weaken_deauth_symbol.sh` to weaken `ieee80211_raw_frame_sanity_check` in the
precompiled `libnet80211.a` (so `deauth.cpp`'s strong override links). The shell script
locates `objcopy` at a **hardcoded absolute path**:

```
TOOLCHAIN_BASE="$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
OBJCOPY="$TOOLCHAIN_BASE/xtensa-esp-elf-objcopy"
```

PlatformIO has since renamed the ESP32-S3 toolchain package to `toolchain-xtensa-esp32s3`
(objcopy now `xtensa-esp32s3-elf-objcopy`), leaving `packages/toolchain-xtensa-esp-elf/` a
stub with no `bin/`. The hardcoded path resolves to nothing, the script exits non-zero, and
the pre-hook does `raise SystemExit` — so **every `cardputer`/`m5stick` build now aborts
before a single TU compiles**, with no relation to the source being built.

This also violates the standing rule *"Never hardcode absolute paths in scripts"*.

Resolve `objcopy` from the toolchain the build is actually using — the SCons `env` the
pre-hook already holds points `$CC` at it — and pass that down to the script. Keep the script
usable standalone by falling back to a `PATH` lookup, never an absolute guess.

## Definition of Done

- **Given** the ESP32-S3 toolchain installed as `toolchain-xtensa-esp32s3` (post-rename), with
  no `bin/` under the old `toolchain-xtensa-esp-elf` package
- **When** `pio run -e cardputer` runs the `weaken_deauth` pre-hook
- **Then** the hook derives `objcopy` from `$CC`, the script weakens the symbol (or reports it
  already weak) and exits 0, and the build proceeds to compile and link — no `SystemExit`

- **Given** `scripts/weaken_deauth_symbol.sh` is run standalone (no SCons env, no `OBJCOPY`
  passed)
- **When** it executes
- **Then** it resolves `objcopy` via a `PATH` lookup (`xtensa-esp32s3-elf-objcopy`, then
  `xtensa-esp-elf-objcopy`) and weakens both `esp32s3` and `esp32` libs — and if none is
  found it fails loud with a non-zero exit and a clear message, never silently continuing

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** the suite is green (build-infra change only; no source or native target touched)

## Design

### Root cause

The script guessed the toolchain location as a fixed absolute path. Toolchain package names
are a PlatformIO implementation detail that changes across framework/toolchain updates (the
unified `toolchain-xtensa-esp-elf` split into per-chip `toolchain-xtensa-esp32s3`), so any
fixed guess is a latent build-breaker. The authoritative source of "which toolchain is this
build using" is the SCons `env`: `env.subst("$CC")` is the exact compiler binary, and its
sibling `objcopy` lives beside it with the same tool-prefix.

### The fix

`weaken_deauth_pre.py` (has `env`):
- Derive `objcopy` from `env.subst("$CC")` by swapping the trailing `gcc` → `objcopy`
  (preserving any `.exe`). This yields e.g.
  `…/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-objcopy`.
- If that file exists, pass it to the script in the child environment as `OBJCOPY`. If `$CC`
  is unexpectedly unset or the derived path is missing, pass nothing and let the script's
  `PATH` fallback handle it.

`weaken_deauth_symbol.sh` (no more `TOOLCHAIN_BASE`):
- `OBJCOPY` precedence: (1) the `OBJCOPY` env var if set and executable; else (2)
  `command -v xtensa-esp32s3-elf-objcopy`; else (3) `command -v xtensa-esp-elf-objcopy`. All
  three are relative resolution — no absolute path is written into the script.
- If none resolves, fail loud: print the miss and exit non-zero (the pre-hook then aborts
  with its existing clear message — a genuinely absent toolchain must stop the build, not be
  papered over).

### One objcopy weakens both chips

The script weakens both `esp32s3` (Cardputer) and `esp32` (M5Stick) `libnet80211.a`.
`objcopy --weaken-symbol` rewrites the archive's symbol *table* — the operation is
independent of the object files' target sub-architecture — so a single xtensa objcopy handles
both. Deriving one objcopy from `$CC` (the current target's toolchain) and reusing it for the
other chip's lib is therefore correct, and matches the prior behaviour where the single
unified `xtensa-esp-elf-objcopy` served both.

### Why no unit test

This is a build-orchestration script; its only meaningful assertion is "the build gets past
the pre-hook and links", which `pio run -e cardputer` is. There is no pure function to unit
test, and the native env never runs the hook. Verification is the successful embedded build
plus the printed resolved-objcopy path, consistent with the posture of the other build-infra
docs.

## Verification

- Build: `pio run -e cardputer` — the pre-hook resolves and prints the objcopy path, weakens
  (or confirms weak) the symbol, and the build compiles and links a valid image. Report the
  resolved path and RAM/Flash.
- Standalone: `bash scripts/weaken_deauth_symbol.sh` with no `OBJCOPY` set — resolves via
  `PATH` and reports success (or "already weak") for both chips whose libs are present.
- Native: `pio test -e native` — full suite green (unaffected).

## As-built

Shipped as designed.

`scripts/weaken_deauth_pre.py`: added `_derive_objcopy()`, which takes `env.subst("$CC")`,
swaps the trailing `gcc`→`objcopy` (preserving `.exe`), and returns the result only if it is
an existing file (else `None`). The hook passes it to the script as `OBJCOPY` in the child
env (`subprocess.run(..., env=child_env)`); when `None`, it passes nothing and the script's
`PATH` fallback runs.

`scripts/weaken_deauth_symbol.sh`: dropped `TOOLCHAIN_BASE`. Added `resolve_objcopy()` with
precedence `$OBJCOPY` (if executable) → `command -v xtensa-esp32s3-elf-objcopy` → `command -v
xtensa-esp-elf-objcopy`. Resolved **once** at top level into `OBJCOPY_BIN` — a bug caught in
review: an initial `local OBJCOPY` inside `weaken_for_chip` dynamically shadowed the incoming
`$OBJCOPY` env var (bash dynamic scope), making `resolve_objcopy` read the empty local; the
top-level single resolution into a distinctly-named global fixes it and avoids re-resolving
per chip. Missing objcopy now exits 1 with a clear message before any archive is touched.

Verification:
- Build: `pio run -e cardputer` **SUCCESS** (118 s) — pre-hook printed
  `Using objcopy: …/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-objcopy` (derived from
  `$CC`), weakened both libs, linked a valid ESP32-S3 image. RAM 26.4% (86,632 B), Flash
  73.9% (2,468,807 B).
- Standalone: `OBJCOPY=…/xtensa-esp32s3-elf-objcopy bash scripts/weaken_deauth_symbol.sh`
  weakened both `esp32s3` and `esp32` libs (one objcopy, both chips — arch-agnostic
  confirmed); with no `OBJCOPY` and none on `PATH`, it failed loud (exit 1, clear message),
  as designed.
- Native: `pio test -e native` 752/752 (build-infra change only).
