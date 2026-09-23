---
id: '0035'
title: "Stop logging captured credentials (and debug pointers) to Serial in the captive portal"
type: slice
status: accepted
date: 2026-09-23
supersedes: []
superseded_by: []
---

## Goal

`ArduinoCaptivePortal::handleLogin()` prints every harvested credential to the serial console
in cleartext ([`arduino_captive.cpp:577`](../src/modules/ap/arduino_captive.cpp)):

```
Serial.printf("[ArduinoCaptive] CREDENTIAL CAPTURED! User: %s, Pass: %s, IP: %s\n",
              cred.username, cred.password, cred.clientIP);
```

This is unconditional (`Serial.printf`, not gated by `CORE_DEBUG_LEVEL`), so it ships in every
build. Anyone with USB access to the device — the operator's own harvested third-party secrets —
can read captured usernames and passwords straight off the serial monitor. Alongside it sits a
cluster of leftover debug instrumentation that prints raw object/member **pointers** (`this=%p`,
`&credentialCount_=%p`) from a past credential-count debugging session: the constructor
(`:289`), `getCredentials()` (`:303`), and two "Before/After increment" lines in `handleLogin()`
(`:572`, `:575`). Pointers aid nothing in production and weaken ASLR-style opacity.

Surfaced by the 2026-09-19 C-level security review and confirmed against source
(2026-09-23). Removing the leak is the fix; the pointer debug lines go with it as the same
cleanup.

## Definition of Done

- **Given** a credential is captured by the captive portal
- **When** the code path runs
- **Then** the serial console shows only a **secret-free** capture event (e.g. a running count),
  never the captured username or password

- **Given** the captive-portal source
- **When** it is grepped for `Pass: %s`, `User: %s`, and the debug pointer prints (`this=%p`,
  `&credentialCount_=%p`)
- **Then** there are zero matches — the credential leak and the leftover pointer instrumentation
  are gone

- **Given** the sniffer/captive capture behaviour
- **When** a credential is captured
- **Then** it is still stored in `credentialsArray_`, `credentialCount_` still increments, the
  `onCredential_` callback still fires, and `saveCredentialsToSD()` still runs — only the logging
  changed, so capture behaviour is identical

- **Given** the cardputer build
- **When** `pio run -e cardputer` runs
- **Then** it compiles and links (the file is device-only; there is no pure logic to native-test,
  and behaviour is unchanged, so no new test — same rationale as slice-0032)

## Design

Pure logging change, no behaviour change:

- `arduino_captive.cpp` — delete the constructor `this=%p` print (`:289`), the `getCredentials()`
  pointer print (`:303`), and the two "Before/After increment" pointer prints (`:572`, `:575`).
- Replace the `CREDENTIAL CAPTURED! User/Pass/IP` line (`:577`) with a secret-free event log:
  `Serial.printf("[ArduinoCaptive] Credential captured (%zu total)\n", credentialCount_);` — the
  operator still gets "a credential was captured" feedback, without the secret.

### No new test, by design

This changes only logging; the capture path (`strncpy` into `credentialsArray_`, `credentialCount_++`,
`onCredential_`, `saveCredentialsToSD`) is untouched, so there is no new behaviour to regression-test
and rule 3 forbids adding a test that only asserts a log string. The proof is the existing suite +
cardputer build staying green and the grep showing the secrets/pointers are gone.

### Boyscout

Two `Serial.printf` tags in the same file misspell the module as `[ArduineCaptive]` (`:347`, `:392`);
fixed to `[ArduinoCaptive]` while here. A separate, pervasive issue — several `Serial.printf` lines in
this file use a literal `\n` (escaped backslash + `n`, e.g. `:548`) instead of a newline — is **out of
scope** here (it spans many lines and is orthogonal to the security fix); logged to the LEDGER under
"Standardize logging" instead.

## Verification

- Grep: `User: %s` / `Pass: %s` / `this=%p` / `&credentialCount_=%p` in
  `src/modules/ap/arduino_captive.cpp` → **0** matches.
- Build: `pio run -e cardputer` — compiles and links; report Flash vs the slice-0034 baseline.
- Native: `pio test -e native` — full suite green (device-only file; no test added).
- No on-device step required: the change is log-only; capture behaviour is provably unchanged.

## As-built

Shipped as designed. `arduino_captive.cpp` lost the constructor `this=%p` print, the
`getCredentials()` pointer print, and the two "Before/After increment" pointer prints in
`handleLogin()`. The `CREDENTIAL CAPTURED! User: %s, Pass: %s, IP: %s` line is replaced by a
secret-free `Serial.printf("[ArduinoCaptive] Credential captured (%zu total)\n", credentialCount_)`.
Capture behaviour is unchanged (store into `credentialsArray_`, `credentialCount_++`, `onCredential_`
callback, `saveCredentialsToSD`). Boyscout: the two `[ArduineCaptive]` typos → `[ArduinoCaptive]`.

Verification:
- Grep: `User: %s` / `Pass: %s` / `this=%p` / `&credentialCount_=%p` in
  `src/modules/ap/arduino_captive.cpp` → **0** matches.
- Build: `pio run -e cardputer` **SUCCESS**, Flash 73.8% (**2,466,099 B**) — a net reduction (the
  removed debug format strings shrink `.rodata`).
- Native: `pio test -e native` **772/772** (device-only file; no test added, per the "no new test"
  rationale above).
- No on-device step: log-only change; the capture path is provably unchanged.
