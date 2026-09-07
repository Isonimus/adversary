---
id: '0013'
title: "Unify ArduinoJson to v7 across all build environments"
type: slice
status: accepted
date: 2026-09-07
supersedes: []
superseded_by: []
---

## Goal

The embedded environments pin `bblanchon/ArduinoJson@^6.21.0` (`platformio.ini:28`) while
the native test environment pins `^7.2.1` (`platformio.ini:150`) — **incompatible majors**.
The native suite therefore validates ArduinoJson v7 semantics, and the two firmware targets
ship v6. Tests and production disagree on a major version of a dependency that parses and
serialises every config file, capture sidecar, WPA-SEC cache entry, and web-server response.

The source compiles under v7 only through the deprecated `StaticJsonDocument<N>` /
`DynamicJsonDocument(N)` aliases, whose meaning **changed** across the major bump: in v6 the
capacity `N` is a hard, fixed allocation (stack for `Static`, one heap block for `Dynamic`)
that **silently truncates** when the payload exceeds it; in v7 both are elastic
heap-allocated `JsonDocument`s and the capacity argument is ignored. So the same call site
behaves differently depending on which env built it — the exact split this slice removes.

The v6 silent-truncation failure mode is not hypothetical here. `server_manager.cpp` already
carries the scar: its summaries response outgrew a fixed `StaticJsonDocument<2048>`, which
dropped entries with no error, and the site was hand-patched to a computed `Dynamic` size
(`server_manager.cpp:486-488`). That is a fixed-capacity bug rescheduled, not fixed —
another growth in payload re-arms it. v7's elastic document retires the whole class.

This slice unifies both firmware targets onto v7, migrates every call site to the plain
`JsonDocument` end state (dropping the dead capacity arguments), and records the
heap-semantics consequence for the no-PSRAM budget.

## Scope

**16 document-declaration sites across 5 files** actually use the ArduinoJson API:

| File | Sites | Native build |
|------|-------|--------------|
| `modules/system/time_manager.cpp` | 2 × `StaticJsonDocument<256>` | compiled (not executed) |
| `modules/storage/wpasec_cache.h` | `DynamicJsonDocument(4096)`, `(2048)` | compiled |
| `modules/storage/handshake_metadata.h` | `StaticJsonDocument<512>`, `<1024>` | compiled |
| `modules/storage/settings_manager.h` | 2 × `DynamicJsonDocument(8192)`, 2 × `StaticJsonDocument<2048>` | `#ifdef ESP32` only |
| `modules/server/server_manager.cpp` | `<512>`, 2 × `<1024>`, 3 × `Dynamic(...)` | `#ifdef ESP32` only |

Four further files (`wardriving/wardriving_exporter.cpp`, `rfid/rfid_audit.cpp`,
`network/wpasec_service.{h,cpp}`, `storage/capture_registry.h`) `#include <ArduinoJson.h>`
but call **no** ArduinoJson API — `wardriving_exporter` writes its JSON by hand with
`printf` precisely to avoid a 150 KB+ document. These stale includes are removed as a
boyscout step, contingent on the build (a translation unit that breaks was relying on the
transitive include and genuinely needs ArduinoJson — its own include is then the correct
fix, and it is kept).

## Definition of Done

- **Given** the split pins above
- **When** `grep ArduinoJson platformio.ini` is run
- **Then** every environment resolves the same major (`^7.2.1`); no `^6` remains

- **Given** the unified v7 pin
- **When** `pio run -e cardputer` and `pio run -e m5stick` are built
- **Then** both compile and link with no error and no `StaticJsonDocument` /
  `DynamicJsonDocument` deprecation warning (every site is plain `JsonDocument`)

- **Given** the migrated source
- **When** `pio test -e native` is run
- **Then** the full suite stays green (the storage/system rename sites are native-compiled,
  so a compile or semantic regression there fails the build)

- **Given** the migration is complete
- **When** the source tree is grepped for `StaticJsonDocument` and `DynamicJsonDocument`
- **Then** neither deprecated alias appears in `src/`

## Design

### One version, chosen forward

v7 is the maintained line; v6 is legacy. The unification goes **forward** (embedded
`^6.21.0` → `^7.2.1`), not backward, for three reasons that compound:

1. **The tested semantics win.** The native suite already exercises v7. Pinning embedded
   back to v6 would make the tests validate a version the firmware does not run — the
   inverse of the point of having them. Moving production to v7 makes the test env
   authoritative.
2. **It retires a silent-failure class.** v6's fixed capacity fails by truncation with no
   error return propagated to most call sites (`doc[...] = value` on a full document is a
   no-op). That violates the fail-loud bar and already caused the `server_manager`
   summaries defect. v7's elastic document cannot truncate — it grows to the payload.
3. **The cost is negligible.** v7's elastic allocator adds a small amount of code — measured
   at +11.7 KB flash on cardputer (`## As built`), +0.3 % against a 72 %-full partition — and
   no static RAM. That is the price of the correctness and stack-headroom wins below, not a
   saving; an earlier draft of this slice predicted a code-size *reduction*, which the build
   refuted.

### Plain `JsonDocument`, capacity arguments dropped

Every site migrates to `JsonDocument doc;`. The `<N>` template parameter and `(N)`
constructor argument are **dead in v7** — the document sizes itself to its content — so
keeping the aliases would preserve misleading numbers and emit deprecation warnings for no
benefit. The computed-capacity expression at `server_manager.cpp:488`
(`1024 + summaries.size() * 96`) is deleted outright: it existed only to hand-size a fixed
`Dynamic` document, a job v7 does elastically. Comments that named a capacity as their
rationale (the pre-allocation heap guards in `handshake_metadata.h`, `wpasec_cache.h`) keep
their *why* — a low-heap board must still refuse a large parse — but stop citing a fixed
byte count the code no longer has.

### Heap semantics on a no-PSRAM board (the decision to record)

v7's `JsonDocument` is **always heap-allocated and elastic**; v6's `StaticJsonDocument<N>`
lived in the enclosing stack frame. The migration therefore moves the former `Static` sites
from stack to heap. On this board that is a net improvement, not a regression:

- **Stack pressure drops.** ESP32 tasks default to an 8 KB stack (CLAUDE.md pitfall #1). A
  `StaticJsonDocument<2048>` (settings) or `<1024>` on an AsyncWebServer / callback stack
  was a standing overflow hazard; moving it to heap removes 1–2 KB from those frames.
- **Fragmentation risk is bounded.** The no-PSRAM budget (~205 KB SRAM) makes heap
  fragmentation a real concern (see the Karma-TLS fragmentation finding). But every one of
  these documents is **short-lived and function-scoped** — allocated, filled or parsed,
  read, and destroyed before the function returns. None outlives its call, so none creates
  a lasting hole. The elastic allocator also requests only what the payload needs, which for
  the common small config/cache entries is *less* than the old fixed capacity.
- **Net static footprint is measured, not estimated** (`## As built`), per the design bar.

There is no path where v7 costs more resident memory than v6 for this usage pattern: the
`Dynamic` sites were already heap; the `Static` sites move off a scarce stack onto a heap
they only touch transiently.

### Why no new native test

Every one of the 16 sites is unreachable by a native *execution* test: the settings and
server sites are wholly inside `#ifdef ESP32`; the time and storage sites compile natively
but early-return before the JSON on `SDManager::isReady()` being false (there is no native
SD, by the project's existing design — `SettingsManager::save()` is stubbed to
`return true`). More fundamentally, the native env is *already* v7, so any behavioural
assertion about elastic growth or truncation would pass **before and after** this change —
it would test ArduinoJson v7, which is already present, not the migration. The quality bar
is explicit that a test which passes before the change proves nothing.

The migration's observable effect is "both firmware targets build and run on a unified v7,
and nothing that was green goes red." Its regression guard is therefore the three-env build
plus the existing native suite: the storage and system rename sites are native-compiled, so
a botched rename or a semantic slip there fails `pio test -e native` at compile time. This
is the same guard, and the same reasoning, as slice-0009's compile-only fix — fabricating an
execution test here would assert something other than what changed.

## Verification

- `pio run -e cardputer` — compiles and links on v7; record flash/RAM delta vs the v6
  baseline captured before the change.
- `pio run -e m5stick` — compiles and links on v7 (the secondary target must not regress).
- `pio test -e native` — full suite green (compile-checks the storage/system rename).
- `grep -rn 'StaticJsonDocument\|DynamicJsonDocument' src/` — no matches.
- `grep -n ArduinoJson platformio.ini` — all envs on `^7.2.1`.
- Recorded in `## As built`.

## As built

Shipped as designed: both firmware pins moved to `^7.2.1`, all 16 document declarations
became plain `JsonDocument`, and the ~18 deprecated `createNestedObject` /
`createNestedArray` calls became `[key].to<T>()` / `array.add<T>()`. `containsKey` was left
as-is — it is *not* deprecated in v7. The computed-capacity hack at `server_manager.cpp`
(`1024 + summaries.size() * 96`) was deleted. All four stale `#include <ArduinoJson.h>`
(wardriving_exporter, rfid_audit, wpasec_service `.h`/`.cpp`, capture_registry) were removed
and both embedded links still succeeded, so no translation unit had been leaning on the
transitive include.

`grep -rn 'StaticJsonDocument\|DynamicJsonDocument\|createNested' src/` returns only two
hits, both in comments that recount the historical fixed-capacity truncation — no live use.

**Measured footprint (the correction to the Design's prediction).** The migration was
expected to shrink code; it did the opposite by a small margin. Static RAM is unchanged on
both targets — as predicted, the `Static`→heap shift is a *runtime* stack/heap change the
linker figure does not show.

| Env | RAM (v7) | Δ RAM | Flash (v7) | Δ Flash vs baseline |
|-----|----------|-------|------------|---------------------|
| cardputer | 86,232 B (26.3 %) | 0 | 2,407,931 B (72.0 %) | +11,684 B (+0.3 %) vs v6 |
| m5stick | 68,312 B (20.8 %) | 0 | 2,381,143 B (75.7 %) | +11,844 B (+0.4 %) vs slice-0009 |

The ~11.7 KB flash growth is v7's elastic allocator; it is negligible against partitions at
72 % / 76 %, and it buys the elimination of the silent fixed-capacity truncation class plus
1–2 KB off the deepest async-callback stack frames. The runtime heap effect (transient,
function-scoped documents that never outlive their call) was reasoned, not instrumented on
hardware — consistent with the project's practice of not fabricating a measurement it did
not take; a field heap check on the web-server and settings paths is the natural follow-up
if fragmentation is ever suspected.

**Builds (all three envs green):**
- `pio run -e cardputer` — **SUCCESS**, RAM 26.3 % / Flash 72.0 %.
- `pio run -e m5stick` — **SUCCESS**, RAM 20.8 % / Flash 75.7 %.
- `pio test -e native` — **702/702**. The storage/system rename sites are native-compiled,
  so this green run is the compile-level regression guard for the migration (per the "Why no
  new native test" reasoning — the native env is already v7, so no execution test could
  fail-before / pass-after here).
