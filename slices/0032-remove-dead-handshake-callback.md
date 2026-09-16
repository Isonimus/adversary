---
id: '0032'
title: "Remove the never-wired HandshakeCapturedCallback from SnifferScreen"
type: slice
status: accepted
date: 2026-09-16
supersedes: []
superseded_by: []
---

## Goal

`SnifferScreen` carries a `HandshakeCapturedCallback` — a `std::function` typedef
(`sniffer_screen.h:144`), a `setOnHandshakeCaptured()` setter (`:187`), an
`m_onHandshakeCaptured` field (`:220`), and a call site inside the packet path
(`sniffer_screen.cpp:637`, fired on any EAPOL frame). **`setOnHandshakeCaptured()` has no caller
anywhere in the tree**, so `m_onHandshakeCaptured` is always the default-constructed (empty)
`std::function` and the `if (m_onHandshakeCaptured && …)` branch is permanently dead. It was
noticed during slice-0028 and left out to keep that commit atomic (logged in the LEDGER).

Remove it rather than wire it, for two independent reasons:

1. **It duplicates a channel that already exists.** Handshake notification is already event-driven:
   `HANDSHAKE_CAPTURED` is published by the dedicated `handshake_capture` module and consumed by
   `notification_manager` (toast). A second, point-to-point `std::function` path set from `main.cpp`
   is exactly the coupling the EventBus replaced (project CLAUDE.md, "Event-Driven Communication").
2. **Wired, it would be a bug, not a feature.** The branch fires on `packet.isEAPOL()` — a *single*
   EAPOL frame — with no handshake-completeness check. A lone EAPOL frame is not a captured
   handshake (M1–M4, or M1+M2 for PMKID; misclassification here is subtle enough that M1+M2 was once
   mislabelled PMKID). Wiring it to a "handshake captured" notification would fire false positives on
   stray EAPOL traffic. The sniffer's honest role is the `m_stats.eapolFrames++` counter it already
   keeps; real handshake detection correctly lives in `handshake_capture`.

The dead `<functional>` include (`sniffer_screen.h:22`) becomes removable too — after this change no
`std::function` remains in the header (boyscout: no unused includes, rule 3).

## Definition of Done

- **Given** the `SnifferScreen` public API
- **When** the codebase is grepped for `HandshakeCapturedCallback`, `setOnHandshakeCaptured`, and
  `m_onHandshakeCaptured`
- **Then** there are zero matches — the typedef, setter, field, and call-site branch are all gone

- **Given** the sniffer packet path
- **When** an EAPOL frame is seen
- **Then** `m_stats.eapolFrames` is still incremented (the counter is retained; only the dead
  notify branch is removed) — behaviour is otherwise unchanged, because the removed branch could
  never execute (the callback was never set)

- **Given** the native suite and both device builds
- **When** `pio test -e native`, `pio run -e cardputer`, and the header's `<functional>` removal are
  run
- **Then** the suite stays green (including `test_packet_sniffer`) and both builds compile and link —
  proving nothing depended on the removed API

## Design

Pure deletion, no replacement:

- `sniffer_screen.h` — delete the `HandshakeCapturedCallback` typedef (`:144`), the
  `setOnHandshakeCaptured()` setter (`:187`), the `m_onHandshakeCaptured` field (`:220`), and the
  now-unused `#include <functional>` (`:22`).
- `sniffer_screen.cpp` — delete the `if (m_onHandshakeCaptured && m_pcapWriter.isOpen()) { … }`
  branch (`:636–639`), keeping the enclosing `if (packet.isEAPOL()) { m_stats.eapolFrames++; }`.

### No new test, by design

This removes permanently-dead code, so there is no new behaviour to regression-test, and rule 3
forbids adding irrelevant or fragile tests. The proof that nothing depended on the API is the
existing suite staying green — `test_packet_sniffer` exercises the packet path, including EAPOL
accounting — plus both device builds still linking. Writing a test that asserts a removed symbol is
absent would test the compiler, not the behaviour.

## Verification

- Grep: `HandshakeCapturedCallback`, `setOnHandshakeCaptured`, `m_onHandshakeCaptured` → 0 matches
  tree-wide; `std::function` → 0 matches in `sniffer_screen.h`.
- Native: `pio test -e native` — full suite green, `test_packet_sniffer` included (unchanged count;
  this slice adds no test).
- Build: `pio run -e cardputer` — compiles and links; report Flash vs the slice-0031 baseline
  (Flash 2,466,663 B). A tiny decrease is expected (the empty `std::function` member is gone).
- No on-device step: the removed branch never executed on-device either (the callback was never
  wired), so there is nothing hardware-observable to confirm.

## As-built

Pure deletion, as designed. `sniffer_screen.h` lost the `HandshakeCapturedCallback` typedef, the
`setOnHandshakeCaptured()` setter, the `m_onHandshakeCaptured` field, and the now-unused
`#include <functional>` (no `std::function` remains in the header). `sniffer_screen.cpp` lost the
dead `if (m_onHandshakeCaptured && …)` branch, keeping `if (packet.isEAPOL()) { m_stats.eapolFrames++; }`.
No replacement and no new test: the branch was permanently dead (the callback was never set), so
handshake notification stays with the existing `HANDSHAKE_CAPTURED` EventBus path.

Verification:
- Grep: `HandshakeCapturedCallback` / `setOnHandshakeCaptured` / `m_onHandshakeCaptured` → **0**
  matches tree-wide; `std::function` in `sniffer_screen.h` → **0**.
- Native: `pio test -e native` **772/772** (unchanged — this slice adds no test; `test_packet_sniffer`
  green, so the EAPOL-accounting path is unaffected).
- Build: `pio run -e cardputer` **SUCCESS**. RAM 26.4% (86,640 B); Flash 73.8% (**2,466,531 B**,
  **−132 B** vs the 0031 baseline of 2,466,663 B — the dead branch and empty `std::function` member
  are gone).
- No on-device step: the removed branch never executed on-device either (the callback was never
  wired), so there is nothing hardware-observable to confirm.
