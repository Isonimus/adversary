---
id: '0012'
title: "Public release: squash history and curate maintainer-only material"
type: architecture
status: accepted
date: 2026-09-06
supersedes: []
superseded_by: []
---

# ADR-0012 — Public release: squash history and curate maintainer-only material

## Context

The repository has been developed privately (378 commits) and is being published at
`github.com/Isonimus/adversary` under the Isonimus identity. It is offensive-security
tooling, which raises the bar on what goes public:

1. **History leak risk.** Publishing the full 378-commit history means trusting that no
   secret, API key, credential, or third-party capture (real BSSIDs/GPS/PII) was ever
   committed and later removed, across every commit. A perfect per-commit audit is
   expensive and error-prone; a scan of the *current* tree being clean does not prove the
   *history* is.
2. **Maintainer-only material.** The repo carries an ADR/slice decision-record workflow
   ("Stele") and its tooling, internal planning docs, an open-work ledger, and Claude Code
   operator config. Outside contributors will not use Stele, and this material is noise (or
   worse, a broken-linter trap) in a public clone.
3. **Public contact without a personal email.** SECURITY.md and CODE_OF_CONDUCT.md need a
   reporting contact, but the maintainer's personal email must not go on a public surface.

## Decision

1. **Squash to a single "initial public release" commit.** The public history begins at one
   commit containing exactly the curated tree. The full pre-public history (378 commits) is
   preserved in an **offline git bundle** kept outside the public repository. This removes
   the history-leak risk without needing a per-commit audit, and gives a clean first release.

2. **Curate the public tree.** Publish the firmware and everything a user or contributor
   needs; keep maintainer-only material local (gitignored, **not** deleted — the maintainer
   keeps using it):

   | Published | Kept local (gitignored) |
   | --- | --- |
   | `src/ test/ data/ lib/`, `platformio.ini`, `sdkconfig.defaults` | `.claude/` (Claude Code operator config) |
   | `README LICENSE DISCLAIMER CHANGELOG CLAUDE.md EPIC.md` | `docs/` (internal EPIC/SPIKE/design docs) |
   | `adr/ slices/ adr/INDEX.md` (decision records, as static artifacts) | `LEDGER.md` (open-work list) |
   | `scripts/weaken_deauth_*` (build-critical) | `scripts/*.mjs` (Stele machinery: lint/index/immutable) |
   | `.github/` (CI, CodeQL, templates, dependabot) | `/adversary/` (on-device runtime output) |

   The decision records (`adr/`, `slices/`) ship as **static artifacts**; the Stele
   *machinery* that maintains them does not. A public clone therefore carries no Stele
   tooling and cannot fail on absent linters, and `CONTRIBUTING.md` tells contributors they
   need not use the workflow.

3. **Public contact = GitHub [@Isonimus](https://github.com/Isonimus)** — used in
   `SECURITY.md` (alongside GitHub private vulnerability reporting) and
   `CODE_OF_CONDUCT.md`. No personal email on any public surface.

## Consequences

- The public repository's `git blame`/history reaches back only to the initial commit;
  granular history lives solely in the offline bundle. The `adr/`/`slices/` records carry
  the "why" that blame would otherwise supply.
- The maintainer continues working in this local repository (which retains the gitignored
  `.claude/`, `docs/`, `LEDGER.md`, and Stele machinery) and pushes normally to the public
  remote afterwards; the squash is a **one-time** operation, not a per-release habit.
- Open-work tracking (`LEDGER.md`) is deliberately off the public repository. If a public
  roadmap or known-issues surface is wanted later, it moves to GitHub Issues/Discussions —
  a separate decision.
- Reversing the squash on the public record is not possible, but nothing is lost: the bundle
  is the complete backup, and a fresh audit could re-derive any excluded file.
- A later change to publish any currently-excluded material (e.g. opening `LEDGER.md` or the
  Stele tooling) is a normal commit, not a second squash.

## Amendment — 2026-10-02: the three Stele doc-linters are tracked, by necessity

An audit of the public tree (prompted while closing a PR whose test tried to read the local
`.claude/hooks/pre-commit`) found one deviation from the curation table above:
`scripts/lint-docs.mjs`, `scripts/build-index.mjs`, and `scripts/check-immutable.mjs` are
**tracked and public** (present since the `e0225f8` "Initial public release" squash), whereas
the table lists `scripts/*.mjs (Stele machinery)` under "kept local".

This is **correct and intended**, and the table's wording was simply too broad:

- **ADR-0018 requires these three to be tracked.** The pre-commit hook validates the *staged*
  copies of exactly these checkers (`$staged/scripts/$checker`), because grading a commit with
  a linter it does not contain is the very defect ADR-0018 removes. Keeping them untracked
  would break the hook. So the "keep local" rule here conflicts with ADR-0018, and ADR-0018
  wins — they must be in the commit.
- **No security cost.** The 2026-10-02 audit confirmed: the public history is squashed to the
  single initial-release root (no 378-commit leak); the maintainer's email/domain appears
  nowhere in the public tree; and no secrets, API keys, or captured data are present. These
  three files are generic, zero-dependency doc-linters — no secrets, no absolute paths.
- **Inert in a public clone.** A public clone carries these scripts but not the hook
  (`.claude/` stays local), and public CI never invokes them (`ci.yml`'s `node-checks` runs
  only `scripts/lint-serial-escapes.*`). They are at worst harmless noise — the "broken-linter
  trap" the original decision feared is not sprung, because nothing auto-runs them there.

**Narrowed rule.** "Kept local" covers Stele machinery *not required tracked by another
decision* — i.e. `.claude/` (the hook and Claude Code operator config) and the ADR/slice
*workflow* as a process. The three doc-linters above are the recorded exception, tracked per
ADR-0018. Project-specific scripts under `scripts/` that are not Stele machinery — e.g.
`lint-serial-escapes.mjs` (a source lint, slice-0037) and the build-critical
`weaken_deauth_*` — are legitimately public and were never in scope for this exclusion.

Everything else in the original table is unchanged: `.claude/`, `LEDGER.md`, `EPIC.md`,
`docs/` (beyond the four published design docs and the README screenshots), and `/adversary/`
remain local.
