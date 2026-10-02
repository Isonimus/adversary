---
id: '0037'
title: "Fix literal \\n in Serial logs, and guard against it with a source lint"
type: slice
status: accepted
date: 2026-10-01
supersedes: []
superseded_by: []
---

## Goal

Nine `Serial.printf` statements across five files end their format string with a
**doubled backslash** — `"...\\n"` — which the compiler turns into the two literal
characters `\` and `n`, not a newline. At runtime the line never terminates, so the
next log output concatenates onto it and line-oriented serial tooling (the pyserial
monitor's `readline()`, see [serial-capture-method](../adr)) buffers across records.
The console becomes a run-on smear exactly when it is most needed — mid-attack, during
a capture, or while importing keys.

Current sites (grep `\\[nrt]` under `src/`):

- [`arduino_captive.cpp`](../src/modules/ap/arduino_captive.cpp): `:435`, `:533`, `:545`, `:593`
- [`handshake_capture.cpp:447`](../src/modules/capture/handshake_capture.cpp)
- [`settings_manager.h`](../src/modules/storage/settings_manager.h): `:812`, `:847`, `:883`
- [`evil_twin.cpp:73`](../src/modules/attack/evil_twin.cpp)

This was spotted and **deferred** by slice-0035 (its Boyscout note logged it to the
LEDGER under "Standardize logging" as out of scope for the credential-leak fix). This
slice is that follow-through — scoped to the escape bug, not the broader
`CORE_DEBUG_LEVEL`/ErrorLogger standardization, which stays deferred.

The two `file.print("\\n")` / `file.print("\\r")` sites in
[`traffic_proxy.cpp:570-571`](../src/modules/sniffer/traffic_proxy.cpp) are **not** bugs:
there the literal `\n` is the intended output (a real newline byte is being escaped into
its two-character JSON representation). The fix and its guard must leave them untouched.

## Definition of Done

- **Given** the serial console during captive-portal, handshake, evil-twin, or key-import activity
- **When** one of the nine statements logs
- **Then** the line ends with a real newline — records no longer run together

- **Given** `src/` is scanned by `scripts/lint-serial-escapes.mjs`
- **When** it runs on the pre-fix tree
- **Then** it reports the nine sites (fails); **and** after the fix it reports zero —
  while still allowing the two intentional `traffic_proxy.cpp` escapes

- **Given** the native suite and the cardputer/m5stick builds
- **When** `pio test -e native` and `pio run` run
- **Then** they stay green (`settings_manager.h` is native-compiled, so the suite exercises it)

## Design

### The fix

Nine edits, `\\n` → `\n`. Pure logging correction, no behaviour change.

### Boyscout — API-key prefix on the console (ties to slice-0035)

The three `settings_manager.h` sites do not just carry the escape bug; they print the
**first 8 characters of an imported API key** to serial as an import confirmation:

```cpp
Serial.printf("[Settings] Importing WPA-SEC key: %s...\n", String(newKey).substring(0, 8).c_str());
```

Slice-0035 established that secrets do not belong on the serial console. An 8-char prefix
of a WPA-SEC / WiGLE / pwncrack token is partial key material disclosed on a line anyone
with USB access can read. While editing these exact lines, replace the prefix preview with
the key **length**, which confirms a successful import without disclosing any key bytes:

```cpp
Serial.printf("[Settings] Importing WPA-SEC key (%u chars)\n", (unsigned)strlen(newKey));
```

### The guard — a source lint, not a mock-Serial test

A test that asserts on captured log strings would test an implementation detail and be
fragile (rule 3 forbids it). The real regression risk is re-introducing the `\\` typo in
source, so the guard belongs at the source level.

**Rule (content-based, call-agnostic):** flag any C/C++ string literal that contains the
two-character sequence `\\n`, `\\r`, or `\\t` — **unless the literal is exactly that one
escape** (`"\\n"` / `"\\r"` / `"\\t"`), which is the deliberate "render a control byte as
its escape" idiom. This is why the rule keys off literal *content*, not the surrounding
call: it must flag `Serial.printf("x\\n")` while allowing `file.print("\\n")`, and both
appear in this codebase.

**Where it runs: CI, alongside `cppcheck` — not the pre-commit hook.** The hook (ADR-0003,
"Stele") is deliberately scoped to the *documentation method* (`adr/`, `slices/`, …); it
never archives `src/`. Source-quality gates already live in CI (`cppcheck` over `src/`,
`ci.yml`). Putting a source lint in the Stele hook would break that separation and expand
the hook's remit; putting it beside `cppcheck` follows the established precedent. Archiving
`src/` into the hook would also be the wrong tool even though it is cheap here (252 files,
~17 ms measured).

The lint's own correctness is covered by `scripts/lint-serial-escapes.test.mjs`
(`node:test`, zero-dependency like the other `scripts/*.mjs`): it asserts the buggy form is
flagged and the lone-escape idiom is allowed. The fail-before/pass-after proof for *this
bug* is the lint run against the real tree (nine → zero).

## Verification

- Lint before fix: `node scripts/lint-serial-escapes.mjs src` → **9 findings** (the sites above).
- Lint after fix: → **0 findings**, with `traffic_proxy.cpp:570-571` not reported.
- Lint self-test: `node --test scripts/lint-serial-escapes.test.mjs` → green.
- Grep: `\\[nrt]` under `src/` → only the two intentional `traffic_proxy.cpp` lines remain.
- Native: `pio test -e native` — full suite green.
- Build: `pio run -e cardputer` — compiles and links; report Flash vs the slice-0036 baseline.
- No on-device step: log-only change on the console; runtime behaviour is otherwise unchanged.

## As-built

Shipped as designed. Nine `\\n` → `\n` corrections across the five files listed in the Goal.
The three `settings_manager.h` sites also dropped the 8-char API-key prefix preview
(`String(key).substring(0, 8)`) for a `strlen`-based length confirmation, per the Boyscout
note — no key bytes reach the console now.

The guard is `scripts/lint-serial-escapes.mjs` (zero-dependency, content-based rule) with
`scripts/lint-serial-escapes.test.mjs` (`node:test`) for its own correctness, wired into
`ci.yml` as a new `serial-escapes` job beside `cppcheck` (self-test, then lint over `src/`).
Not added to the Stele pre-commit hook, which stays scoped to the documentation method.

Verification:
- Lint fail-before (against the HEAD tree): **9 findings, exit 1** — the nine sites.
- Lint pass-after (working tree): **0 findings, exit 0**; `traffic_proxy.cpp:570-571` not reported.
- Lint self-test: `node --test scripts/lint-serial-escapes.test.mjs` → **6/6**.
- Grep `\\[nrt]` under `src/` → only the two intentional `traffic_proxy.cpp` lines remain.
- Native: `pio test -e native` → **784/784** (`test_settings_manager` green; the file is native-compiled).
- Build: `pio run -e cardputer` → **SUCCESS**, Flash **2,467,307 B** — **−304 B** vs the
  slice-0036 baseline (2,467,611 B); the removed `substring`/`String` work and shorter
  format strings shrink `.text`/`.rodata`.
- No on-device step: log-only change; runtime behaviour is otherwise unchanged.

Out of scope, still deferred (LEDGER): the broader logging standardization — gating these
`Serial.printf` calls behind `CORE_DEBUG_LEVEL`/ErrorLogger rather than shipping them
unconditionally.
