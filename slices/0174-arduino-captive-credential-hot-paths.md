---
id: '0174'
title: "arduino_captive credential-get hot-path allocations"
type: slice
status: accepted
date: 2026-11-27
supersedes: []
superseded_by: []
---

## Goal

Close the last v0.12.0 Boyscout from the RAM-reclaim audit (LEDGER line 216): the
`ArduinoCaptivePortal::getCredentials()` return-by-value path and the two attack-module
converters that call it. The ledger note cited `arduino_captive.h:173 → const-ref` and
`credentials_.reserve(16)` in the ctor, but reading the file showed line 173 is the
`#ifndef UNIT_TEST` stub (the real class ends at line 100), and the real class stores
credentials in a fixed `ArduinoCapturedCredential credentialsArray_[MAX_CREDENTIALS]`
rather than a vector, so neither the const-ref nor the reserve applies as written. The
actual hot path is: `EvilTwin::getCredentials()` and `KarmaAP::getCredentials()` each
call `arduinoPortal_.getCredentials()` on every UI tick, which copies the fixed array
into a freshly-built `std::vector` just so the converter can iterate it. Two surgical
wins: (a) have the converters iterate `getCredentialsArray()` + `getCredentialCount()`
directly, skipping the intermediate vector entirely; (b) `reserve(credentialCount_)`
inside `ArduinoCaptivePortal::getCredentials()` so any residual caller also stops
reallocating as it push_backs.

## Definition of Done

- **Given** a running Evil Twin session with N captured credentials
- **When** the UI tick calls `EvilTwin::getCredentials()`
- **Then** it iterates the fixed array directly, no intermediate `std::vector` is
  allocated from the Arduino portal, and the returned (static) `convertedCreds` vector
  is reserved to `N` before the conversion loop

- **Given** a running Karma session with N captured credentials
- **When** the UI tick calls `KarmaAP::getCredentials()`
- **Then** same: iterates the array via count, no portal-side vector allocation, static
  destination reserved before the loop, and the existing LOG_D line still reports the
  credential count correctly

- **Given** any (future) caller of `ArduinoCaptivePortal::getCredentials()` itself
- **When** that method runs with N captured credentials
- **Then** the returned vector is `reserve`d to `N` before the push loop, so a single
  allocation covers the fill

## Design

Searched for every caller of `arduinoPortal_.getCredentials()` under `src/` and
`test/`: exactly two (`evil_twin.cpp:370` and `karma_ap.cpp:719`), both sitting in
functions that return a `const std::vector&` to a `static convertedCreds` vector built
by iterating the Arduino portal's credential list. Considered deleting
`ArduinoCaptivePortal::getCredentials()` entirely since it has no in-tree callers
after the rewrite; rejected to keep behavioural parity with the `#ifndef UNIT_TEST`
stub at `arduino_captive.h:165`, which still exposes it. Rule of three: this is two
nearly identical converter bodies (EvilTwin + Karma) with a LOG line that differs,
so the duplication stays (global §3, rule of three) — a helper would need three
sites to earn its place.

The `.reserve(credentialCount_)` added inside `getCredentials()` is a one-line
defensive win for any future caller; it is not load-bearing for the Evil Twin /
Karma path once the converters stop using it.

No API change: `EvilTwin::getCredentials()` and `KarmaAP::getCredentials()` still
return `const std::vector<ap::CapturedCredential>&` to the same `static convertedCreds`.
Screens continue to call those unchanged.

## Verification

- `pio test -e native` stays green (no new unit test: the behaviour under test is
  "returned vector matches the fixed array", which is already covered by the
  credentials-captured flow exercised through EvilTwin / KarmaAP integration paths;
  the change is a strict allocation-count reduction, not a semantic one).
- `pio run -e m5stick -j 2` and `pio run -e cardputer -j 2` both link clean under the
  ADR-0083 warnings gate.
- Manual read-through of `evil_twin.cpp:365-381` and `karma_ap.cpp:714-734` to
  confirm (a) `convertedCreds.reserve()` precedes the push loop, (b) the loop uses
  array + count (no intermediate `const auto&` vector binding), (c) the LOG_D in
  Karma still prints `credentialCount` correctly.

## As built

Shipped exactly as designed. `src/modules/ap/arduino_captive.cpp:302-309` now
`reserve`s `credentialCount_` before the fill loop. `src/modules/attack/evil_twin.cpp:365-381`
and `src/modules/attack/karma_ap.cpp:714-734` iterate
`arduinoPortal_.getCredentialsArray()[0..getCredentialCount())` directly, and
`convertedCreds.reserve(count)` precedes the conversion loop in each; the Karma
`LOG_D` line reads `count` rather than `arduinoCreds.size()`. Native suite green;
both xtensa envs built serially under ADR-0083. LEDGER line 216's Boyscout
sub-mention removed in the same commit.
