---
id: '0039'
title: "Report per-target RAM/Flash usage in CI"
type: slice
status: accepted
date: 2026-10-02
supersedes: []
superseded_by: []
---

## Goal

CI builds `cardputer` and `m5stick` on every push and PR but never surfaces the
resulting binary size. Growth toward a partition ceiling is therefore **invisible until
a build fails** — and on the `m5stick` that failure has no soft landing: it uses
`huge_app.csv` (3 MB app, **no OTA**, [`platformio.ini:99`](../platformio.ini)), so a
full partition is a hard build error, not a degraded update path.

Make per-target RAM/Flash a visible, trackable signal on every run, so the trend is
legible and a StickC feature-freeze line can later be set on evidence rather than guessed.

## Measured baseline (2026-10-02, this branch's parent)

| Target | Partition | Flash used | Usage | Headroom |
| --- | --- | ---: | ---: | ---: |
| `cardputer` | ~3.19 MB (default) | 2,467,307 B | 73.8% | ~855 KB |
| `m5stick` | 3.0 MB (`huge_app`, no OTA) | 2,412,707 B | 76.7% | **~716 KB** |

The `m5stick` is the binding constraint, as expected. RAM is comfortable on both
(cardputer 26.5%, m5stick 21.0% of 320 KB).

## Decision

**Report-only. No hard ceiling gate in this slice.**

Emit a per-leg RAM/Flash table (used / total / usage% / headroom) to the GitHub Step
Summary for the two firmware legs; the `native` leg is skipped (it links a host binary,
so its "flash" is meaningless).

Why report-only rather than a fail-gate now:

- **~716 KB / 23% headroom** on the tightest target means a gate would sit green and only
  add brittleness. There is no cliff to guard against yet.
- **No growth-rate data exists.** Any threshold chosen today is a guess. The ledger item
  that seeded this work explicitly wants the freeze line set *deliberately*, and warns
  that the "~2.47 MB used" figure on record was the **cardputer** build, not the stick —
  exactly the kind of mix-up a premature gate would bake in.
- **Report-only already delivers the ask** ("growth is invisible") on the first run.

### Mechanism

- The build/test step tees its output to `build.log`. GitHub's bash shell runs with
  `pipefail`, so a failing `pio` still fails the step through the `tee`.
- A follow-up step, guarded `if: matrix.env != 'native'`, greps the `RAM:`/`Flash:`
  summary lines PlatformIO already prints, computes headroom, and writes a Markdown table
  to `$GITHUB_STEP_SUMMARY`. The summary is per-job, so each firmware leg renders its own
  block.
- **Fail-loud, per the project bar.** If a *successful* firmware build yields no `Flash:`
  line (PlatformIO output-format drift), the step emits a `::error::` annotation and exits
  non-zero — a broken size signal is surfaced, never silently emptied. A missing `RAM:`
  line warns but does not fail.
- **Zero extra build cost.** The report parses the build step's own output; it does not
  re-invoke `pio run`.

This change is confined to [`.github/workflows/ci.yml`](../.github/workflows/ci.yml). It is
not a Stele pre-commit concern (the hook never archives `.github/`), and it needs no source
or native-test change.

## Definition of Done

- **Given** a push or PR that builds `cardputer` or `m5stick`
- **When** the leg finishes a green build
- **Then** its job's Step Summary carries a RAM/Flash table with used, capacity, usage%,
  and headroom for that target

- **Given** the `native` leg
- **When** it runs
- **Then** no size table is produced (the host binary has no meaningful flash)

- **Given** a green firmware build whose output carries no `Flash:` line (format drift)
- **When** the size step runs
- **Then** it emits a `::error::` annotation and fails the leg — the broken signal is
  surfaced, not silently emptied

## Rejected alternatives

- **A hard `m5stick` ceiling gate in v1** — rejected: no growth data, so the threshold
  would be arbitrary and risk false-red. Deferred to a fast-follow once the trend is known
  (ledgered).
- **A separate size-report job** — rejected: it would rebuild the firmware to read a number
  the build already prints, doubling CI time per target.
- **Parsing `firmware.elf` with the Xtensa `size` tool** — rejected: PlatformIO already
  prints exactly these figures; re-deriving them is redundant and couples the report to the
  toolchain path.

## Fast-follow (ledgered, not in this slice)

Once a handful of runs establish the `m5stick` growth slope, add a *deliberate* ceiling
gate — fail the `m5stick` leg past a measured line — which is the enforcement half of the
original ledger item. Report-only first makes that threshold evidence-based.

## Verification

As-built in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml): the matrix build
step became `${{ matrix.cmd }} 2>&1 | tee build.log`, and a new **Build size** step
(guarded `if: matrix.env != 'native'`) parses `build.log` and appends the per-target
RAM/Flash table to the job summary, failing loud if the `Flash:` line is absent after a
green build.

- **YAML**: `ci.yml` parses as valid YAML.
- **Happy path**: the step body, run against the real `pio run` output for both targets,
  produced the baseline table above — `m5stick` Flash 76.7%, headroom 733,021 B (715 KB).
- **Fail-loud path**: a synthetic `build.log` with the `Flash:` line removed tripped the
  `::error::` annotation and exited 1.
- **No source/native impact**: the change is confined to `.github/workflows/ci.yml`; the
  native suite and the firmware builds are untouched, and the first CI run on this branch
  confirms both legs still build and now emit the table.
