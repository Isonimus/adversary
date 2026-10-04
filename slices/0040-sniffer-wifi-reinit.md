---
id: '0040'
title: "Sniffer re-initialises WiFi on start; fails loud on a dead driver"
type: slice
status: accepted
date: 2026-10-03
supersedes: []
superseded_by: []
---

## Goal

The Packet Sniffer goes permanently deaf — captures **0 packets** — after any attack
launched from it (Handshake, Evil Twin, …) is exited, and does not recover across a full
stop/restart or even leaving and re-entering the Sniffer screen. Only a reboot fixes it.

Found on-device 2026-10-03 (Cardputer ADV) while verifying the Scanner/Sniffer attack
handoff (slice-0028). Serial, around the failure:

```
[Sniffer] Started on channel 1
[Sniffer] Stopped. Captured 752 packets        <- healthy before the Handshake
[Sniffer] Executing action Handshake Capture on <hidden>
...
[E][STA.cpp:530] disconnect(): STA disconnect failed! 0x3001: ESP_ERR_WIFI_NOT_INIT
[Sniffer] Started on channel 1
[Sniffer] Stopped. Captured 0 packets           <- deaf, and stays deaf
[Main] WiFi not initialized, skipping reset
```

## Root cause

A chain of three facts, each verifiable in the source:

1. `PacketSniffer` is a process-lifetime **singleton** (`getInstance()`,
   [`packet_sniffer.cpp`](../src/modules/sniffer/packet_sniffer.cpp)), so its
   `m_initialized` flag survives across Sniffer-screen entries.
2. The Handshake screen's teardown calls `WiFi.mode(WIFI_OFF)`
   ([`handshake_screen.cpp`](../src/ui/screens/handshake_screen.cpp)). Through the Arduino
   layer that **deinitialises** the IDF WiFi driver — afterwards `esp_wifi_get_mode()`
   returns `ESP_ERR_WIFI_NOT_INIT` (hence `[Main] WiFi not initialized, skipping reset`).
3. On the next `start()`, `m_initialized` is still `true`, so `init()` early-returns and
   never re-runs the Arduino `WiFi.mode(WIFI_STA)` that would re-init the driver. `start()`
   then used the **raw** `esp_wifi_set_mode(WIFI_MODE_STA)`, which cannot revive a
   deinitialised driver, and `enablePromiscuous()` — which **already returns `false`** on a
   `NOT_INIT` driver ([`wifi_utils.h`](../src/utils/wifi_utils.h)) — had its result
   **discarded**. Capture was armed on a dead driver and silently delivered nothing.

The Scanner survives the identical `WiFi.mode(WIFI_OFF)` because it re-inits every entry
through `WiFi.scanNetworks()` (Arduino, init-on-demand). The Sniffer trusted a cached flag
plus a raw-IDF mode set, so it alone got stuck.

Two project-bar violations compounded: trusting a cached flag that cannot know the driver
was torn down elsewhere, and **failing silent** — claiming `RUNNING` while capturing zero.

## Decision

Fix at the consumer, minimally and at root:

1. **`start()` ensures the driver via the Arduino layer, not a cached flag or raw IDF.**
   Replace the raw `esp_wifi_set_mode(WIFI_MODE_STA)` with `WiFi.mode(WIFI_STA)`, which
   re-initialises the driver when returning from a `WIFI_OFF`. This is the load-bearing
   fix: it makes a start correct regardless of what tore the radio down between sessions.
2. **Fail loud.** Check `enablePromiscuous()`'s return; on `false`, log an error, leave the
   state `STOPPED`, and return `false` so the screen reports a failed capture instead of a
   silent zero.
3. **Reset `m_initialized` in `stop()`** for coherence — the flag must not outlive the
   driver state it claims to describe.

Rejected: resetting WiFi from the attack-teardown side (every attack would have to know
every later consumer's needs — wrong coupling); and a `HardwareManager`-owned WiFi
lifecycle (real, but the deferred architecture item, not this bug's scope).

## Definition of Done

The regression test is on-device. The defect lives entirely in ESP32 WiFi-driver lifecycle
code. The native suite cannot
instantiate `PacketSniffer` (its header is ESP32-coupled; `test_packet_sniffer` re-defines
types locally for exactly that reason), so a native "unit test" could only re-implement the
fix and assert against the copy — a duplicate, fragile test the quality bar forbids. The
honest regression gate is the on-device repro, which is deterministic:

- **Given** a Cardputer ADV running this firmware
- **When** the Sniffer captures, an attack (Handshake) is launched from a captured packet,
  then exited back to the Sniffer, and the Sniffer is started again
- **Then** it captures a non-zero packet count again (was 0 before this fix)

- **Given** a start where the WiFi driver genuinely cannot be brought up
- **When** `enablePromiscuous()` returns `false`
- **Then** the Sniffer logs an error and reports a failed start — it never claims to run
  while capturing nothing

The native suite is still run to prove no regression elsewhere.

## Verification

- Native suite green (no regression).
- Cardputer build green.
- On-device: the repro above produces a non-zero capture after a Handshake round-trip.
