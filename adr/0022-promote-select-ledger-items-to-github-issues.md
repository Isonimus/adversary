---
id: '0022'
title: "Promote select ledger items to GitHub Issues (promote, don't mirror)"
type: architecture
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

# ADR-0022 — Promote select ledger items to GitHub Issues (promote, don't mirror)

## Context

The repository is now public (ADR-0012) and has its first external signals: a first
stargazer and a first outside-filed issue. Public contribution needs a public surface —
onboarding newcomers, a visible roadmap, coordination, and PR-to-work linking — and GitHub
Issues is that surface.

Two prior decisions constrain how we may use it:

- The open-work ledger (`LEDGER.md`) is the repo's **single** open-work tracker. The
  single-mutable-ledger rule (`stele:ADR-0001`) deliberately forbids a second tracking
  file, on the stated ground that *two files require manual sync and manual sync does not
  happen*.
- ADR-0012 kept `LEDGER.md` **off the public repository** (gitignored). A contributor
  cannot see it, so the private ledger cannot, by itself, recruit or coordinate anyone. That
  same ADR anticipated this exact decision: *"If a public roadmap or known-issues surface is
  wanted later, it moves to GitHub Issues/Discussions — a separate decision."* This is that
  decision.

The tension is real: copying the ledger wholesale into Issues would recreate precisely the
two-tracker manual-sync problem `stele:ADR-0001` exists to prevent. The resolution is to
change *which surface owns an item*, never to keep the same item alive in both.

## Decision

1. **Issues are a public, external-contributor surface — not a mirror of the ledger.** The
   ledger remains the single internal worklist and the **default** home for open work. Most
   items never become issues.

2. **Promote, don't mirror.** When an item is a genuine fit for outside contribution, it
   **moves** ledger → issue: the issue becomes that item's single home, and its ledger line
   is **deleted in the same change**. One item lives in exactly one tracker. This preserves
   `stele:ADR-0001`'s intent (one place per item, no manual sync) while honoring ADR-0012's
   public-surface allowance.

3. **Promotion criteria — an item is eligible only if all three hold:** (a) it is legible to
   an outsider **without deep internal or project context**; (b) it is **self-contained and
   bounded**; (c) it genuinely **wants an outside contributor**. Label `good first issue`
   when it is additionally small and teaches the codebase; otherwise `enhancement`.

4. **Explicitly not promoted — these stay in the ledger:** on-device verification items that
   need maintainer-owned hardware; architecture and design deferrals; security, supply-chain,
   and watch-list audits; and bugs whose diagnosis needs deep project state. As public issues
   these would rot or mislead a newcomer, so they remain internal.

5. **One-way at promotion time.** The direction is ledger → issue only. A closed or declined
   issue does **not** return to the ledger — its outcome is recorded by the normal
   done-record (the PR / git log), exactly as a deleted ledger line is. An internal-only
   follow-up discovered while working an issue is a **new** ledger line, not a copy-back of
   the issue.

## Consequences

- There are now two open-work surfaces — internal (`LEDGER.md`, private) and external
  (Issues, public) — but **never the same item in both**, so `stele:ADR-0001`'s no-sync
  guarantee holds unbroken. The invariant to protect is single-ownership per item, not
  single-file.
- Logging or grooming an item now carries a promote-or-keep call; the default is keep, and
  promotion is the deliberate exception the criteria above gate.
- Issues become the public roadmap surface ADR-0012 foresaw: the ledger's "publish a short
  public roadmap" and "add good-first-issue labels; recruit contributors" items are
  satisfied by a curated set of labelled issues, and those ledger lines close when done.
- A richer process later (Discussions, a Projects board, milestones) is a normal extension
  of this decision — **unless** it reintroduces mirroring the same item across surfaces,
  which this ADR forbids. Reopening that requires a new ADR superseding this one.
