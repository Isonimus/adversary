---
id: '0015'
title: "Scope CodeQL to our own sources by filtering vendored results"
type: slice
status: accepted
date: 2026-09-07
supersedes: []
superseded_by: []
---

## Goal

Make the Code Scanning tab a true signal. The first CodeQL run on the public repo raised
**24 alerts, 23 of them in vendored libraries** compiled into `.pio/libdeps/` (M5GFX,
M5Unified, FastLED, IRremoteESP8266, NimBLE/tinycrypt) — code we neither own nor patch. One
alert, and only one, is in our own `src/`. A tab that is 96 % third-party noise trains the
maintainer to ignore it, which defeats the point of having it (and buries the next real
finding).

The existing `codeql-config.yml` *looks* like it already prevents this — it sets
`paths: [src]` / `paths-ignore: [lib, test]`. **Those keys are inert for compiled
languages.** CodeQL's C/C++ analysis covers everything built during the workflow, and
`pio run -e cardputer` compiles the whole dependency tree; `paths`/`paths-ignore` only
filter interpreted languages (Python/Ruby/JS). So the config gave a false sense of scoping
while 23 vendored alerts sailed through. This slice replaces the illusion with a mechanism
that actually works, and fixes the one real finding.

## Definition of Done

- **Given** a CodeQL run that has analyzed the firmware (vendored libraries included, as it
  must to compile)
- **When** results are prepared for upload
- **Then** every result located under `.pio/`, `lib/`, or `test/` is dropped, so only
  alerts in our own `src/` reach the Security tab

- **Given** the one real `src/` alert (`cpp/comparison-with-wider-type` at
  `captures_screen.cpp`)
- **When** the code is fixed at root
- **Then** the loop bound is a like-typed `int16_t`, the width mismatch is gone, and the
  firmware still builds (`pio run -e cardputer` SUCCESS)

- **Given** a future alert that lands in `src/`
- **When** CodeQL runs
- **Then** it is **not** filtered — the exclusion is by vendored path only, so real findings
  in our code always surface

## Design

### Why post-analysis SARIF filtering (not `paths-ignore`, not a build trick)

Two documented ways exist to scope a compiled-language scan; only one fits:

- *Build only your code* — infeasible. The firmware genuinely depends on those libraries to
  compile; there is no build of `src/` that excludes them.
- *Filter results after analysis* — drop unwanted results from the SARIF before upload. This
  is the correct lever here.

So the CodeQL step is split: `analyze` runs with `upload: false` and writes SARIF to a
directory, a filter step removes vendored results by path, and `upload-sarif` publishes the
cleaned file. The filter is a one-line **`jq`** program, not the `advanced-security/filter-
sarif` action — keeping the workflow free of third-party actions, consistent with the `gh`-
only posture of the release workflow (slice-0014). It drops any result whose primary
location URI starts with `.pio/`, `lib/`, or `test/` (the three the original config *meant*
to exclude), and is null-safe so a result without a location is kept, never crashed on.

### The config file stops lying

`codeql-config.yml` keeps its pinned `config-file` reference but loses the inert
`paths`/`paths-ignore` keys; a comment states plainly that path filtering does nothing for
compiled languages and points to the SARIF filter as the real mechanism. An inert setting
that reads as protection is worse than none.

### The one real finding is fixed, not dismissed

`drawCredentialView` compared `int16_t y` against `canvas.height() - FOOTER_HEIGHT -
lineHeight` (an `int`/`long` expression) — a genuine width mismatch, though benign in
practice (screen coordinates, no truncation or overflow possible at these magnitudes). Fixed
by hoisting the bound into a `const int16_t` — like-typed comparison, and a named bound
reads clearer than the inline expression. This mirrors the existing pattern a few hundred
lines up where the same `canvas.height() - FOOTER_HEIGHT - …` is already assigned into an
`int16_t`. It is a type-clarity change with **no observable behaviour change**, so it needs
no regression test — the rendering path is device-bound and not natively executable, and a
test asserting an unchanged screen loop bound would test nothing real.

## Verification

- **The `jq` filter is proven on synthetic SARIF.** Given results at `.pio/…`, `lib/…`,
  `test/…`, `src/…`, and one with no location, the filter keeps exactly the `src/` result
  and the location-less one and drops the three vendored ones — confirmed locally before
  wiring it in, so the workflow runs the exact expression validated here.
- **The code fix builds:** `pio run -e cardputer` SUCCESS after hoisting the bound.
- **Local (the ceiling here):** `codeql.yml` parses as valid YAML with the split
  analyze→filter→upload steps and pinned actions; the doc linter passes.
- **Remote / real run:** the authoritative proof is the next CodeQL run on push — the
  Security tab should drop from 24 open alerts to the single `src/` one, which this slice
  fixes, i.e. to zero once merged. Recorded as a LEDGER follow-up until observed.

## As built

_To be completed once the post-merge CodeQL run is observed and the alert count confirmed;
tracked as a LEDGER follow-up until then._

## Amendment — 2026-09-07: first post-merge run observed green

Merged to `main` as `a6e2444` and pushed on 2026-09-07. The CodeQL run on that commit
completed `success`, and the open Code Scanning alert count dropped from **24 to 0** — the 23
vendored-library alerts filtered out of the SARIF before upload, and the one genuine `src/`
finding (`cpp/comparison-with-wider-type`) resolved by the `int16_t` bound. All three DoD
scenarios hold: vendored results are dropped, the real finding is fixed and the firmware
still builds, and the filter is by vendored path only — so a future `src/` alert would still
surface.
