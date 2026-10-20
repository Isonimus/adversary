---
id: '0081'
title: "Clear the compiler-warning backlog in our own src/ (quick-tidy + truncation review)"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

Close the ledger `[feature]` that a pre-existing compiler-warning backlog has accumulated
in our own `src/`. A clean `pio run -e cardputer` (vendored `lib/` and the vendored
in-`src/` trees excluded: `src/common`, `src/fl`, `src/lgfx`, `src/class`, `src/platforms`,
`src/third_party`, `src/M5Unified.cpp`) emits warnings across eight classes. Seven of them
are our own code and low-risk; the eighth (`-Wdeprecated-declarations`) is upstream-API
migration work of a different character and risk.

This slice takes the seven low-risk classes only: the "make our own code warning-clean"
half. The measured set (ground truth from a forced `src/`-only recompile, 2026-10-20):

| Class | Physical sites | Files |
|---|---|---|
| `-Wformat=` (benign `%u`/`%d` vs `uint32_t`) | 9 | capture_registry.h (737,755), beacon_spam.cpp (218), pcap_writer.cpp (92,103,275), handshake_screen.cpp (1024), server_status_screen.cpp (128,142) |
| `-Wmissing-field-initializers` | 9 | menu_factory.cpp (122,132,145,147,148), usb_hid_keyboard.cpp (34,39,46), time_manager.cpp (124) |
| `-Wformat-truncation=` | 4 | captures_screen.cpp (875), rfid_screen.cpp (1102), saved_networks_screen.cpp (86), whitelist_screen.cpp (226) |
| `-Wreorder` | 1 | ble_spanner.{h,cpp} (m_layout vs m_lastModelIdx) |
| `-Wvolatile` | 1 | ble_spanner.cpp (499,510) |
| `-Wunused-variable` | 1 | wpasec_cache.h (61,65) |
| `-Wcomment` | 1 | module_detection.h (13) |

**Explicitly deferred, not in this slice:** `-Wdeprecated-declarations` — ArduinoJson
`containsKey` in `handshake_metadata.h` (223,229) and the `server_manager.cpp` HTTP-API
calls (14 sites). Those are semantic upstream migrations (each wants endpoint testing) and
land as their own follow-up slice. **Sequencing note:** a `-Werror` ratchet scoped to our
`src/` (the agreed end-state, ADR + CI gate) can only switch on once `src/` is *fully*
warning-clean, so it lands after *both* this slice and the migrations slice, never after
this one alone. It is deliberately not wired here.

## Definition of Done

- **Given** a forced `src/`-only recompile (`rm -rf .pio/build/cardputer/src && pio run -e
  cardputer`) currently emits warnings in our own `src/` across the seven classes above
- **When** this slice's fixes are applied and the same recompile is run
- **Then** our own `src/` emits **zero** warnings of those seven classes, and the only
  remaining `src/` warnings are the deferred `-Wdeprecated-declarations` set (unchanged in
  count) plus vendored-tree warnings outside our files.

- **Given** the four `-Wformat-truncation=` sites, whose values are domain-bounded (RFID
  sector index 0..16/16; saved-network, whitelist and sync-queue counts bounded well below
  the `int` range GCC assumes)
- **When** each is reviewed and fixed
- **Then** each is confirmed a false positive (no real truncation is reachable), and the
  destination buffer is sized to the format's worst case so the warning clears by making
  the buffer genuinely large enough — not by suppressing the diagnostic.

- **Given** the `-Wvolatile` BLE connection counter
- **When** `BLESpanner::onConnect` / `onDisconnect` run
- **Then** the counter still increments on connect and decrements on disconnect
  (behaviour identical), with no deprecated volatile compound operator.

- **Given** the native unit suite
- **When** `pio test -e native` runs after the changes
- **Then** it still reports 844/844 (the shared files touched — `capture_registry.h`,
  `wpasec_cache.h`, `module_detection.h` — change format/annotation only, no behaviour).

## Design

### What exists, and what this reuses

- **House format idiom.** The `-Wformat=` warnings are all the same benign case: on this
  target `uint32_t` is `long unsigned int`, so `%u`/`%d` mismatches it. The codebase already
  fixes this exactly one way — `%lu` with an `(unsigned long)` cast — at **13** existing
  sites (`beacon_spam_screen.cpp`, `probe_flood_screen.cpp`, `evil_twin_screen.cpp`, …).
  `PRIu32`/`<cinttypes>` appears at **0** sites. So this slice follows the established idiom
  rather than introducing a second convention. The LOG-macro sites
  (`capture_registry.h:737,755`, `beacon_spam.cpp:218`, `pcap_writer.cpp:92,103`) report at
  `log.h:71` because the `Serial.printf` lives in the macro, but the format string and args
  are the caller's: the fix is at each call site, and `log.h` itself is correct and
  untouched.
- **The N/M footer-counter pattern** (`"%d/%d"` index/total) recurs at the four truncation
  sites plus `handshake_screen.cpp:1024`. That is a rule-of-three DRY candidate, and it was
  **considered and deliberately not extracted here**: the call targets differ
  (`setRightContent` vs `showToast` vs `printf`), the value types differ (`int`
  `selectedIndex()+1` vs `uint32_t` counters), and the slice's job is to clear warnings, not
  reshape UI helpers. Extracting a shared formatter now would be speculative generality (the
  wrong abstraction costs more than the duplication). Left as a possible future tidy, not
  folded in.

### Per-class fix

- **`-Wformat=`** — at each of the 9 sites, change the `uint32_t`-bound specifier to `%lu`
  (or `%02lu`) and cast the argument to `(unsigned long)`. Mechanical, no behaviour change.
- **`-Wmissing-field-initializers`** —
  - `KeyReport report = {0}` / `struct tm ... = {0}` → `= {}` (value-initializes every
    member; the explicit field assignments that follow are unchanged). `{0}` named only the
    first member, which is the warning.
  - The five `CarouselItem` brace-inits in `menu_factory.cpp` omit the 6th member
    (`std::function<bool()> enabledFn`, no default). Add a trailing `, nullptr` so the
    always-enabled entries state their (empty) predicate explicitly, matching the RFID
    entry that already supplies all six.
- **`-Wformat-truncation=`** — reviewed individually (see DoD): all four are false positives
  on domain-bounded values. Fix by sizing each destination buffer to the format's worst case
  so no input can truncate, with a one-line WHY where the sizing is non-obvious. Not a
  `#pragma`/suppression: the buffer is made actually sufficient (fail-safe), which is the
  root-cause fix for "this buffer might be too small."
- **`-Wreorder`** — move `m_layout(KeyboardLayout::US)` up in the `BLESpanner` constructor
  init list so member-init order matches declaration order (`m_layout` is declared before
  `m_lastModelIdx`). Init order is already correct for the values; this only silences the
  order mismatch the compiler flags.
- **`-Wvolatile`** — rewrite `m_connectedCount++` / `m_connectedCount--` as
  `m_connectedCount = m_connectedCount + 1` / `- 1`. The member stays `volatile` (its
  existing contract is unchanged); only the C++20-deprecated compound form is replaced.
  Switching to `std::atomic` would change concurrency semantics and is out of scope.
- **`-Wunused-variable`** — `heapBefore` / `heapAfter` in `wpasec_cache.h::clearForSSL` are
  read only inside `LOG_D`, which compiles to nothing below debug level, so they are unused
  in release builds. Mark both `[[maybe_unused]]`. The measurement is intentional and kept
  (fail-loud: not deleted, just annotated as deliberately level-gated).
- **`-Wcomment`** — `module_detection.h:13` contains the literal `/*` inside the phrase
  "not a `core/*.cpp`" within a block comment, which reads as a nested comment opener. Reword
  to drop the `/*` (e.g. "not a `core/` translation unit").

### What is intentionally not done

No `-Werror` flag is added, no CI gate, no `platformio.ini` change: this slice only removes
the warnings. The gate is the agreed end-state but is blocked on the migrations slice (above).

## Verification

- **Primary (the DoD proof):** `rm -rf .pio/build/cardputer/src && pio run -e cardputer`,
  then grep the captured log for `warning:` in our own `src/` (excluding vendored trees):
  the seven classes drop to **0**; only the deferred `-Wdeprecated-declarations` set remains.
  Record the before (measured 2026-10-20) and after counts.
- `pio run -e m5stick` — SUCCESS (the StampS3 twin compiles the same sources).
- `pio test -e native` — 844/844 (shared files `capture_registry.h`, `wpasec_cache.h`,
  `module_detection.h` change format/annotation only; the embedded-only files
  — `ble_spanner`, `usb_hid_keyboard`, the screens — are proven by the two device builds,
  not native, since native excludes them).
- Doc gate: `node scripts/build-index.mjs` + `node scripts/lint-docs.mjs` green with this
  slice added.
- No on-device step: every change is a format specifier, an initializer, a buffer size, an
  init-list order, an annotation, or comment text — none alters runtime behaviour.

## As built

All four Definition-of-Done scenarios were met. 26 physical edit points across 15 files,
all seven low-risk classes; the `-Wdeprecated-declarations` set was left untouched for the
follow-up migrations slice.

- **Warning count (the primary proof).** A forced `src/`-only recompile before the fixes
  emitted the seven classes across our own `src/`; after the fixes, the same recompile
  emits **zero** of them. The only `src/` warnings that remain are the deferred
  `-Wdeprecated-declarations` (unchanged: `handshake_metadata.h` 223/229 and
  `server_manager.cpp` ×14). Measured 2026-10-20.
- **Per class, as applied.** `-Wformat=` → `%lu` + `(unsigned long)` at all 9 sites
  (house idiom; `log.h` untouched, fixed at each caller). `-Wmissing-field-initializers`
  → `{0}`→`{}` for `KeyReport`/`struct tm`, and a trailing `, nullptr` on the eight
  non-lambda `CarouselItem` entries (the RFID/RADIO lambda entries already supplied the
  predicate and were left alone). `-Wformat-truncation=` → all four confirmed false
  positives on domain-bounded values; buffers sized to the format's worst case
  (`msg[40]`→`[48]`, three `"%d/%d"` buffers → `[24]`) with a WHY comment, not suppressed.
  `-Wreorder` → `m_layout` moved up the `BLESpanner` init list to match declaration order.
  `-Wvolatile` → the `++`/`--` on the volatile counter rewritten as explicit
  `= x ± 1` (type and contract unchanged). `-Wunused-variable` → `[[maybe_unused]]` on the
  level-gated heap-measurement locals (kept, not deleted). `-Wcomment` → the literal `/*`
  in "a core/*.cpp" reworded to "a core/ TU".
- **Verification (all green).** cardputer `[SUCCESS]`, Flash 74.6% (2,494,487 B, 20 B
  *below* the pre-fix 2,494,507 B — no bloat); native `844/844`; m5stick `[SUCCESS]`; doc
  lint 0 errors. The shared files (`capture_registry.h`, `wpasec_cache.h`,
  `module_detection.h`) touched format/annotation only, confirmed by the unchanged native
  count; the embedded-only files are covered by the two device builds.
- **Minor deviations from the plan.** (1) `pcap_writer.cpp:92` kept its existing
  `static_cast<uint32_t>(linkType)` and wrapped it in the `(unsigned long)` cast rather
  than collapsing to one cast — correct, marginally verbose. (2) The survey flagged a fifth
  `"%d/%d"` site (`rfid_screen.cpp:1336`); GCC did **not** warn there (it proved the bound
  safe), so it was left untouched rather than widened speculatively.
- **Not done, by design.** No `-Werror` flag, no CI gate, no `platformio.ini` change: the
  gate is blocked on the migrations slice (`src/` must be fully clean first).
