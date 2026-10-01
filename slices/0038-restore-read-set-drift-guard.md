---
id: '0038'
title: "Restore the read-set drift guard the hook and linter both cite"
type: slice
status: accepted
date: 2026-10-01
supersedes: []
superseded_by: []
---

## Goal

Both [`.claude/hooks/pre-commit`](../.claude/hooks/pre-commit) and
[`scripts/lint-docs.mjs`](../scripts/lint-docs.mjs) cite `test/read-set.test.mjs` as the
guard that keeps two lists equal:

- the hook's **archive list** — the `for candidate in …` pathspec it materialises from the
  staged tree (`adr slices LEDGER.md CLAUDE.md README.md docs .claude/commands scripts
  package.json`), and
- the linter's **`READ_SCOPE`** — every path `lint-docs.mjs` actually reads, exported in
  machine-readable form.

If the linter starts reading a path the hook does not archive, the hook grades a commit
against a file it never extracted — the exact defect the citations describe happening once
already ("rules 14 and 15 shipped reading four paths the hook did not copy, and were dead
there for a release while passing in CI"). The named guard was meant to make the next such
omission fail a test instead of going quiet.

**The file was never written.** `git log` shows no history for `test/read-set.test.mjs`;
the two comments describe a test that does not exist. The lists happen to be equal today, so
nothing is broken *now* — but the drift guard the tooling advertises is absent, so the next
divergence would again slip through silently. Found 2026-10-01 while scoping slice-0037.

## Definition of Done

- **Given** the hook's archive list and the linter's `READ_SCOPE`
- **When** `test/read-set.test.mjs` runs
- **Then** it passes while they match, and **fails** if either side gains or loses a path

- **Given** the hook's `for candidate in …` line is renamed or restructured so the list
  can no longer be located
- **When** the test runs
- **Then** it fails loudly (a guard that cannot find its input must not silently pass)

- **Given** CI
- **When** the node checks run
- **Then** `test/read-set.test.mjs` is among them

## Design

### Write the test (chosen)

`test/read-set.test.mjs` reads `.claude/hooks/pre-commit`, extracts the `for candidate in
…; do` list by regex, imports `READ_SCOPE` from `scripts/lint-docs.mjs`, and asserts the two
are set-equal (order is irrelevant: the hook builds a pathspec and `inReadScope()` membership
is unordered, so comparing as sets tests what actually matters and avoids a brittle
order-coupling). If the `for candidate in` line cannot be found, the test fails rather than
vacuously passing — a guard blind to its own input is worse than none.

Importing `lint-docs.mjs` is side-effect-free under the test runner: its CLI guard
(`realpathSync(process.argv[1]) === …`) is false when `argv[1]` is the test file, so `main()`
does not run.

### Rejected: single source of truth

Having the hook derive its archive list from `READ_SCOPE` at runtime would remove the
duplication outright, but the hook needs the list to know *what to archive* before it has
extracted any file from the staged tree — it cannot import the staged `lint-docs.mjs` to
learn what to stage. Reading `READ_SCOPE` out of the staged blob by hand (`git cat-file` +
parse) is as brittle as the regex this test uses, with more moving parts. Two lists kept
equal by a test is the design the tooling already documents; this slice honours it rather
than re-architecting the hook.

### Where it runs: CI

The hook checks the staged tree in a tmpdir; running a node test of the hook against itself
there is extra plumbing for no gain, and the citations explicitly frame this as the guard
that holds "while passing in CI". It joins the existing node checks in `ci.yml`.

## Verification

- `node --test test/read-set.test.mjs` → passes (lists equal today).
- Induce a mismatch (add a bogus entry to `READ_SCOPE` **or** the hook list) → the test
  **fails**, naming the symmetric difference; revert → passes. (Fail-before/pass-after for
  the guard's own validity, since the lists are currently in sync.)
- Rename the hook's `for candidate in` token → the test fails with "could not locate the
  archive list", not a false pass.
- CI runs it alongside the other node checks.

## As-built

Shipped as designed. `test/read-set.test.mjs` parses the hook's `for candidate in …; do`
list, imports `READ_SCOPE` from `scripts/lint-docs.mjs`, and asserts set-equality, reporting
the symmetric difference (`onlyInHook` / `onlyInScope`) on failure; a second case guards
against the regex capturing nothing. It fails loudly if the archive-list line cannot be
found. No vendored file changed — the hook's and linter's existing citations became accurate
once the file existed, so no comment edits were needed.

CI runs it: the former `serial-escapes` job is generalised to `node-checks`, which now runs
both node test files (`node --test scripts/lint-serial-escapes.test.mjs test/read-set.test.mjs`)
and then the `src` escape lint.

Verification:
- `node --test scripts/lint-serial-escapes.test.mjs test/read-set.test.mjs` → **8/8**.
- Induced drift (`'BOGUS_DRIFT'` appended to `READ_SCOPE`) → the read-set test **fails**,
  naming `onlyInScope: ['BOGUS_DRIFT']`; reverted → **2/2** pass. Guard proven to catch the
  divergence it exists for.
- `scripts/lint-docs.mjs` left byte-identical after the induced-drift check (`git diff` clean).
