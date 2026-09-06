---
id: '0011'
title: "Repository infrastructure for public release"
type: slice
status: accepted
date: 2026-09-06
supersedes: []
superseded_by: []
---

## Goal

Make the repository fit to be public and to receive outside contributions. Today it has
source, a README, a LICENSE, a DISCLAIMER, a CHANGELOG and (as of slice-0010) CI — but none
of the community-health and supply-chain scaffolding a public project needs: no contribution
guide, no security-reporting policy, no code of conduct, no issue/PR templates, no dependency
automation, and no static analysis beyond "does it compile". This slice adds that scaffolding
and fixes the README rot that a public reader would hit on arrival.

Static analysis is **CodeQL + cppcheck** (the two chosen for this project): CodeQL for
semantic security queries over our C/C++, cppcheck for fast local-style bug-finding.

The public contact for security and conduct is the maintainer's GitHub handle,
**@Isonimus** — not a personal email (deliberate: no personal address on a public surface).

This slice is the last piece before the initial public push; the push itself — history
squash and the curation of maintainer-only material (`.claude/`, `docs/`, `LEDGER.md`, the
Stele `scripts/*.mjs`) out of the public tree — is a separate decision recorded in ADR-0012.

## Definition of Done

- **Given** a fresh reader of the public repository
- **When** they open the README
- **Then** it shows live status badges (CI, CodeQL, license) and contains no false or
  dangling statements — the m5stick target is described as building (slice-0009), no link
  points at a file curated out of the public tree, and the clone URL is the real one

- **Given** a would-be contributor
- **When** they look for how to contribute, report a bug, request a feature, or report a
  vulnerability
- **Then** `CONTRIBUTING.md`, the issue forms, the PR template, `SECURITY.md` and
  `CODE_OF_CONDUCT.md` are present, consistent with each other, and route to @Isonimus

- **Given** every push and pull request
- **When** CI runs
- **Then** a `cppcheck` leg gates on cppcheck's default `error` severity over `src/`, and a
  CodeQL workflow analyses the C/C++ and reports to the Security tab

- **Given** the GitHub Actions the workflows depend on
- **When** a new version is published
- **Then** Dependabot opens an update PR (the one ecosystem it can track here)

- **Given** the two new workflow files and the edited `ci.yml`
- **When** validated locally
- **Then** they are syntactically valid YAML and structurally well-formed GitHub Actions
  workflows (jobs, steps, pinned actions), to the extent verifiable without a remote

## Design

### Community-health files

- **`CONTRIBUTING.md`** — leads with the authorized-use gate (this is offensive tooling; see
  DISCLAIMER), then PlatformIO setup and the three env commands, points at `CLAUDE.md` for
  the engineering bar, states conventional-commits + one-logical-change, and explicitly tells
  contributors they do **not** need to touch `adr/`/`slices/` (the maintainer's decision-record
  workflow) — a public reader will see those directories and must not be scared off by them.
- **`SECURITY.md`** — the crucial scoping for an offensive tool: a "vulnerability" is a defect
  in the *firmware itself* that harms its operator (dashboard auth/traversal, an API-key TLS
  leak, RCE/data-loss), **not** the attack capabilities, which are the intended function.
  Private reporting via GitHub Security Advisories, fallback @Isonimus. Alpha ⇒ only latest
  `main` supported.
- **`CODE_OF_CONDUCT.md`** — Contributor Covenant 2.1 verbatim, enforcement contact @Isonimus.

### Templates

- **`.github/PULL_REQUEST_TEMPLATE.md`** — summary/closes, type, a testing block naming the
  three envs, and a checklist that includes the CLAUDE.md standards and the authorized-use ack.
- **`.github/ISSUE_TEMPLATE/bug_report.yml`** / **`feature_request.yml`** — GitHub issue
  *forms* (structured), with a device dropdown covering Cardputer ADV / original / StickC.
- **`.github/ISSUE_TEMPLATE/config.yml`** — disables blank issues; contact links to the
  Security-Advisory "new" URL and the DISCLAIMER.

### Supply chain

- **`.github/dependabot.yml`** — `github-actions` weekly, and **only** that. There is
  deliberately no `pip` entry (no Python manifest exists; PlatformIO is installed ad hoc in
  CI) and no PlatformIO entry (Dependabot does not parse `platformio.ini`). A `pip` update
  block with nothing to update is inert config, so it is omitted rather than shipped dead.

### Static analysis

- **`.github/workflows/codeql.yml` + `.github/codeql/codeql-config.yml`** — CodeQL `c-cpp`.
  CodeQL traces a real compilation, so the workflow installs PlatformIO and runs
  `pio run -e cardputer` between `init` and `analyze` (autobuild cannot drive PlatformIO).
  The config restricts analysis to `src/` (paths) and ignores `lib/` (vendored bruce libs)
  and `test/` — we analyse our own code, not third-party code we will not patch. Runs on
  push/PR to `main` and weekly. Findings go to the Security tab; the job is green unless the
  build or analysis itself errors.
- **cppcheck leg in `ci.yml`** — a separate `cppcheck` job (not a matrix entry: it needs an
  `apt` install and different steps, so folding it into the env→cmd matrix would contort it).
  It gates on cppcheck's **default `error` severity** (`--error-exitcode=1`, no `--enable`) —
  the high-signal, low-false-positive class (null deref, buffer overrun, use-after-free) —
  scoped to `src/` only. Rationale for starting at `error` and not `warning`/`style`: those
  broader levels on a large existing codebase are a coin-flip between "dead-red on day one"
  and "a linter nobody enforces"; the baseline could not be measured in the dev environment
  (no cppcheck, no sudo), so the gate starts at the level that is meaningful without being
  reckless, and widening it is a tracked follow-up (LEDGER).

### README

Fix the rot a public reader hits first, and add the badge row:
- Badges (CI, CodeQL, MIT license) under the title.
- Remove the `docs/logo.png` `<img>` — the file was never tracked (broken image today) and
  `docs/` is curated out of the public tree anyway.
- Correct the m5stick status: slice-0009 made it build and slice-0010's CI exercises it;
  the "currently broken / does not compile on master" warnings are now false.
- Repoint the clone URL to `https://github.com/Isonimus/adversary.git`.
- Drop links to files curated out of the public tree (`LEDGER.md`, `docs/…`); fold the
  duplicated inline "Contributing" steps into a pointer to `CONTRIBUTING.md`.

### Why no native unit test for this slice

Like slice-0010, this slice is documentation, community-health config, and CI wiring — there
is no application logic to exercise. Its correctness is YAML/structure validation plus the
doc linters, with the first real CI run on GitHub as the ultimate confirmation.

## Verification

- **cppcheck baseline — measured, not estimated.** cppcheck 2.13 was run locally against
  `src/`. The default-`error`-severity gate reported exactly one finding:
  `tls_upload.cpp [comparePointers]` on the `x509_crt_bundle_end - x509_crt_bundle_start`
  subtraction — a **false positive** on the standard linker-boundary-symbol idiom
  (`_binary_*_start/_end` bound one contiguous embedded blob; their difference is its size).
  It is handled with an in-source `// cppcheck-suppress comparePointers` carrying the WHY,
  not by loosening the gate. After the suppression the gate exits 0, so the CI cppcheck leg
  is green from the first run.
- **Local (the ceiling for the rest):** the two new workflows and the edited `ci.yml` parse
  as valid YAML and are structurally well-formed (triggers, jobs, steps, pinned action
  majors); the issue forms and `dependabot.yml`/`codeql-config.yml` parse as YAML. The Stele
  doc linters (`lint-docs.mjs`, `build-index.mjs`, `check-immutable.mjs`) pass and
  `adr/INDEX.md` is regenerated to include this slice. `pio test -e native` passes and
  `pio run -e cardputer` builds — the only `src/` change is the no-op cppcheck suppression in
  `tls_upload.cpp` (a named local + comment; identical behaviour), so no regression test is
  warranted and the cppcheck gate itself guards that finding class.
- **Remote / real run:** the CodeQL build/analysis is only confirmable on GitHub Actions
  (it traces a real `pio run -e cardputer`, already green locally). Same local-ceiling
  boundary slice-0010 documented, closed by watching the first push.

## As built

Shipped as designed. Community-health files: `CONTRIBUTING.md` (authorized-use gate →
PlatformIO setup → CLAUDE.md standards → conventional commits → the "you don't need to touch
adr/slices" note), `SECURITY.md` (offensive-tool scoping + GitHub private reporting →
@Isonimus), `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1, contact @Isonimus). Templates:
`.github/PULL_REQUEST_TEMPLATE.md`, `.github/ISSUE_TEMPLATE/{bug_report,feature_request}.yml`
(issue forms, device dropdown) + `config.yml` (blank issues off; links to the Security-Advisory
"new" URL and DISCLAIMER). Supply chain: `.github/dependabot.yml` (github-actions weekly only;
pip/platformio deliberately omitted as inert). Static analysis: `.github/workflows/codeql.yml`
+ `.github/codeql/codeql-config.yml` (c-cpp, `src/` only, builds cardputer between init and
analyze) and a `cppcheck` job added to `ci.yml` (default-error gate over `src/`). `.editorconfig`
added. README: badge row (CI/CodeQL/license/status), broken `docs/logo.png` removed, m5stick
corrected from "broken" to "secondary (builds, in CI)", clone URL → Isonimus, links to
curated-out files (`LEDGER.md`, `docs/…`) dropped, inline Contributing steps folded into a
`CONTRIBUTING.md` pointer, project-tree updated to show `adr/`+`slices/` instead of `docs/`.

One `src/` change fell out of the work: the cppcheck false positive in `tls_upload.cpp` was
suppressed in place with an explanatory comment (behaviour unchanged).
