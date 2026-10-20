---
id: '0080'
title: "Refresh the dependency lock and gate it against release staleness"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

Close the ledger `[bug]` that `DEPENDENCIES.lock.md` has drifted from reality: it is
stamped "Last verified: 2026-09-08" (pre-v0.3.0-alpha) and records the native suite at
706/706, while the suite is now 844/844 (+138 tests across the 0.3.0 and 0.4.0 cycles).
The snapshot is the project's anti-drift artifact ("a clone must build the same bytes we
tested"), so an out-of-date snapshot is the one defect it exists to prevent, turned on
itself.

Two halves, both from the ledger line:

1. **Refresh the snapshot** to the measured present: re-resolve all three envs, re-run the
   native suite, re-stamp the date/count.
2. **Add a CI staleness gate** so the artifact can't silently drift again: fail the build
   when the lock's "Last verified" date predates the latest release tag, i.e. a release was
   cut without re-verifying the lock.

## Definition of Done

- **Given** the resolved trees from `pio pkg list` for `cardputer`, `m5stick`, `native`
  and the native suite result
- **When** `DEPENDENCIES.lock.md` is refreshed
- **Then** its "Last verified" line reads `2026-10-20 — native 844/844`, and every env's
  recorded tree still matches `pio pkg list` for that env (verbatim, no version edits).

- **Given** a lock whose "Last verified" date is strictly older than the latest release
  tag's date
- **When** `node scripts/lint-dep-lock-staleness.mjs` runs
- **Then** it exits non-zero and names both the lock date and the newer tag.

- **Given** a lock whose "Last verified" date is on or after the latest release tag's date
- **When** the lint runs
- **Then** it exits zero.

- **Given** a lock with no parseable `Last verified: YYYY-MM-DD` line
- **When** the lint runs
- **Then** it fails loud (non-zero, explicit message) rather than passing by default.

- **Given** the new `dep-lock-staleness` CI job
- **When** CI runs on a push/PR
- **Then** the lint's self-test (`node --test scripts/lint-dep-lock-staleness.test.mjs`)
  asserts the stale/current/same-day/missing-date cases, and the lint runs against the
  real lock.

## Design

### What exists, and what this reuses

- **The lock itself** (`DEPENDENCIES.lock.md`) already states its own refresh procedure
  ("Bump: … update this snapshot in the same commit") and `pio pkg list` as the verify
  command. Re-running those three `pkg list` invocations is the measurement; it showed the
  `cardputer`/`m5stick`/`native` trees and the platform/toolchain block **unchanged** from
  what the lock records. So the refresh is date + count only — there is no version drift to
  re-table. That the versions held is itself the finding the snapshot should capture.
- **The lint pattern** is lifted verbatim from the two existing zero-dependency source
  lints, `scripts/lint-serial-escapes.mjs` and `scripts/lint-raw-serial.mjs`: a pure
  exported core + a thin CLI `main()` guarded by `import.meta.url === file://…`, a sibling
  `*.test.mjs` run with `node --test`, and a dedicated CI job beside `serial-escapes` /
  `raw-serial`. This is the third instance of that shape, so it follows the established
  idiom rather than inventing one. No shared lint-harness is extracted: the three scripts
  share a form, not logic.

### The staleness lint (`scripts/lint-dep-lock-staleness.mjs`)

Separates the pure decision from the IO so the decision is unit-testable without a git
fixture (the other lints test their pure `scanText`, not their fs walk):

- `parseLockDate(text)` — pulls the ISO date out of `Last verified: (YYYY-MM-DD)`. Throws
  on no match (fail loud: a lock we cannot date is a broken lock, not a pass).
- `evaluate({ lockDate, tagDate, tagName })` — pure. `tagDate === null` (no release tags)
  is a non-stale pass with a visible notice: a repo with no release cannot have a lock
  stale *relative to a release*. Otherwise stale ⇔ `lockDate < tagDate`, compared as ISO
  `YYYY-MM-DD` strings (lexicographic order == chronological for that fixed-width format,
  so no `Date`/timezone parsing). Same-day (`lockDate === tagDate`) is a pass.
- `main()` — reads the lock, shells `git for-each-ref --sort=-creatordate
  --format='%(creatordate:short)\t%(refname:short)' refs/tags/v*`, takes the first row,
  and calls `evaluate`. A git failure throws (in CI git is always present; a crash here is
  a broken gate, which must be loud, not a silent green).

Why "date vs latest release tag" and not a full tree-diff in CI: the ledger scoped it to
the date check, and it is the robust, low-false-positive signal. A tree re-diff would mean
re-parsing `pio pkg list` output against the markdown tables on every run — brittle against
PlatformIO's output format (the same fragility the CI size step already guards with an
explicit format-changed error). The date gate instead enforces the *discipline* ("re-verify
the lock whenever you tag a release") that keeps the tree-diff done by a human, which is
where `## Bump` already puts it.

### CI wiring

A new `dep-lock-staleness` job mirrors `serial-escapes`: self-test, then run the lint. It
needs tags present, so its `actions/checkout@v7` sets `fetch-depth: 0` (the default shallow
checkout carries no tags, which would make the gate a false green). That coupling is
commented at the job so a future checkout bump can't quietly re-break it.

## Verification

- `node --test scripts/lint-dep-lock-staleness.test.mjs` — asserts: flags a lock older than
  the tag (naming both), passes a lock newer than the tag, passes same-day, passes with no
  tags, and throws on a lock with no `Last verified` line.
- `node scripts/lint-dep-lock-staleness.mjs` against the refreshed real lock — exits 0
  (lock 2026-10-20 ≥ tag `v0.4.0-alpha` 2026-10-19).
- `pio pkg list -e {cardputer,m5stick,native}` — trees match the refreshed lock.
- `pio test -e native` — 844/844.
- `pio run -e cardputer` and `pio run -e m5stick` — both SUCCESS (the "builds SUCCESS"
  claim the lock's stamp makes).
- Doc gate: `node scripts/build-index.mjs` + `node scripts/lint-docs.mjs` green with this
  slice added.
- No on-device step: this touches build metadata and CI only, no firmware behavior.

## As built

Both halves shipped as designed.

- **Lock refreshed.** `DEPENDENCIES.lock.md`'s stamp moved `2026-09-08 — native 706/706`
  → `2026-10-20 — native 844/844`. Re-resolving all three envs with `pio pkg list` showed
  the `cardputer`/`m5stick`/`native` trees and the platform/toolchain block **unchanged**,
  so no version table was edited: the refresh is the date, the count, and a note that the
  706 → 844 growth is the RFID-in-`hal/` and wardriving-BLE suites, not drift. A bullet
  documenting the new gate was added to the lock's procedure list.
- **Staleness gate.** `scripts/lint-dep-lock-staleness.mjs` with a pure, unit-tested core
  (`parseLockDate` throws on an undateable lock; `evaluate` compares ISO dates as strings,
  treats same-day and no-tags as passes) and a git-backed `main`. Self-test
  `scripts/lint-dep-lock-staleness.test.mjs` (6 cases). CI job `dep-lock-staleness` mirrors
  `serial-escapes`, with `fetch-depth: 0` so tags are present (commented against a future
  checkout bump re-breaking it).
- **No deviations.** The "versions may have drifted" possibility the Design flagged did not
  materialise (they held), which is the Design's anticipated no-edit path.
- **Verification (all green):** native `844/844`; lint self-test `6/6`; lint vs the real
  lock exit 0 ("current with latest release v0.4.0-alpha (2026-10-19)"); a stale fixture
  exit 1 naming both dates and the tag; doc lint 79 docs / 0 errors; `pio run -e cardputer`
  SUCCESS (Flash 74.6%, 2,494,507 B); `pio run -e m5stick` SUCCESS. No on-device step: this
  is build metadata and CI only.
- **Follow-up spun out.** The clean cardputer build surfaced a ~51-site pre-existing
  compiler-warning backlog in our `src/` (none in this slice's files); it was ledgered the
  same day and taken as its own next slice rather than folded in here.

