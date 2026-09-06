---
id: '0010'
title: "CI: build every target and run native tests on push/PR"
type: slice
status: accepted
date: 2026-09-06
supersedes: []
superseded_by: []
---

## Goal

Stand up continuous integration so that a broken build or a failing test is caught
automatically, not months later by accident. The immediate motivation is concrete:
`m5stick` had been un-compilable since the multi-radio cap subsystem landed and **nobody
noticed, because nothing built it** (slice-0009). A green build on a secondary target is
worthless if it is never exercised. CI turns "do all targets still compile and do the tests
pass" from a thing discovered by luck into an enforced gate on every change.

Run, on every push and pull request:
- `pio test -e native` — the 698-case unit suite.
- `pio run -e cardputer` — the primary deployment firmware.
- `pio run -e m5stick` — the secondary target, now that slice-0009 made it build again.

## Definition of Done

- **Given** a push or pull request to the repository
- **When** the CI workflow runs
- **Then** it executes all three legs (native tests, cardputer build, m5stick build) in
  parallel and fails the check if any leg fails

- **Given** the workflow file
- **When** it is validated locally
- **Then** it is syntactically valid YAML and structurally a well-formed GitHub Actions
  workflow (jobs, steps, pinned action versions), to the extent verifiable without a remote

- **Given** a contributor who breaks a target's compile or a unit test
- **When** they push
- **Then** the red check makes the regression visible immediately — the anti-rot guarantee
  that slice-0009's fix depends on to stay fixed

## Design

### Shape: one matrix, three legs

A single job with a 3-entry `matrix.include`, each entry naming an `env` and the `cmd` to
run (`pio test` for native, `pio run` for the two firmwares). `fail-fast: false` so one
leg's failure does not cancel the others — we want to see every target's status on a red
push, not just the first to fail. This is DRY (one job definition, three parallel runs)
without contorting a matrix that mixes `test` and `run` commands.

### Runner and toolchain

`ubuntu-latest`. Steps: `actions/checkout` → `actions/setup-python` → cache
`~/.platformio` + `~/.cache/pip` → `pip install platformio` → run the leg's command. All
action versions are pinned to current stable majors (`checkout@v4`, `setup-python@v5`,
`cache@v4`).

The PlatformIO cache matters here more than usual: this project pulls its platform
(pioarduino) and its Arduino libs (the bruce precompiled bundle with the weak-symbol deauth
patch) from GitHub release zips — hundreds of MB plus the Xtensa toolchain. The cache key is
keyed on `hashFiles('platformio.ini')` so it invalidates exactly when the platform/lib/env
definitions change, and per-leg so the native and embedded toolchains do not thrash one
cache entry. The `weaken_deauth_pre.py` pre-script runs unchanged in CI (it is plain Python
invoked by PlatformIO).

### Concurrency

A `concurrency` group keyed on the workflow + ref, with `cancel-in-progress`, so a rapid
series of pushes to the same branch does not pile up redundant runs — only the latest ref
state is worth building.

### Why no native unit test for this slice

This slice *is* build/CI configuration; there is no application logic to exercise. Its own
correctness is a YAML/structure validation plus, ultimately, a real run on GitHub — see the
verification boundary below.

## Verification

- **Local (the ceiling here):** the workflow parses as valid YAML (pyyaml) and is
  structurally well-formed — `on` triggers present, one job, a 3-entry matrix, pinned
  actions, each leg's command correct. The three commands themselves are already proven this
  session: `pio test -e native` (698/698), `pio run -e cardputer` (SUCCESS), `pio run -e
  m5stick` (SUCCESS) all pass locally, so the workflow runs commands known to be green on a
  clean checkout.
- **Remote / real run:** the remote (`git@github.com:Isonimus/adversary.git`, an empty
  public repo) is added as part of this work, so the workflow *will* execute — its first run
  is triggered by the initial push of `main`. Local validation (pyyaml + structural review;
  no `actionlint` on this host) is the pre-push gate; the first push is the real
  confirmation. The three commands are already green locally on a clean tree, so the run
  exercises known-good commands on a fresh checkout — the remaining unknown is only the CI
  environment (toolchain download, cache), which the first run settles.

## As built

Shipped as designed. `.github/workflows/ci.yml` defines a single `check` job with a 3-entry
`matrix.include` (`native` → `pio test -e native`, `cardputer`/`m5stick` → `pio run -e …`),
`fail-fast: false`, on `push` + `pull_request`, with a `concurrency` group
(`${{ github.workflow }}-${{ github.ref }}`, `cancel-in-progress: true`). Steps:
`actions/checkout@v4` → `actions/setup-python@v5` (3.x) → `actions/cache@v4` over
`~/.platformio` + `~/.cache/pip` (key `pio-<os>-<env>-<hash(platformio.ini)>`, with a
`restore-keys` prefix for partial hits) → `pip install --upgrade platformio` → the leg's
command.

**Local validation (the ceiling here):** parsed with pyyaml and asserted structurally — `on`
carries both `push` and `pull_request`; exactly one job `check`; the matrix legs map to the
three expected commands; the three actions are pinned to `@v4`/`@v5`/`@v4`; `fail-fast` is
false; `cancel-in-progress` is true. The three commands are each proven green on a clean
tree this session (native 698/698, cardputer SUCCESS, m5stick SUCCESS).

**Real run:** the remote `git@github.com:Isonimus/adversary.git` (an empty public repo) is
added alongside this slice, so the workflow executes for real on the initial push of `main`.
Local pre-push validation was pyyaml + manual structural review (no `actionlint` on the
host); the first push is the authoritative confirmation. The first run's result is recorded
as a LEDGER follow-up until observed green.
