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
