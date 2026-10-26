---
id: '0127'
title: "Crash log records the dying heap via an RTC flight recorder"
type: slice
status: accepted
date: 2026-10-26
supersedes: []
superseded_by: []
---

## Goal

Make `/adversary/logs/crash.log` describe the heap *at the crash*, not at the next boot.
Today every entry shows a healthy boot-time reading, so an OOM PANIC/WDT leaves no trace
of why it happened. Capture free heap and largest-free-block (fragmentation) shortly
before the fault and stamp each entry with a real timestamp, implementing the lightweight
half of ADR-0126. The `esp_core_dump` backtrace path stays a separate, deferred slice.

## Definition of Done

- **Given** a PANIC/WDT reset with a heap sample taken this session
- **When** the device reboots and `logResetReason` runs
- **Then** the entry reports the sampled pre-crash free heap, largest free block, and min
  free heap, with the sample's wall-clock time, not a boot-time reading.

- **Given** a reset where RTC memory did not survive (cold power-on, brownout, or a crash
  before this session's first sample)
- **When** the entry is written
- **Then** the heap section reads `unavailable`, never a stale or boot-time number.

- **Given** a clean reset (POWERON / SW / DEEPSLEEP)
- **When** `logResetReason` runs
- **Then** nothing is logged and the consecutive-crash counter is cleared, as before.

## Design

Per ADR-0126:

- `core/crash_report.{h,cpp}` is the pure core: `classifyReset(ResetReason)` and
  `formatCrashEntry(...)`. Host-tested, no `<esp_system.h>`.
- `system_manager.cpp` holds a `RTC_NOINIT_ATTR HeapRecorder` (magic + `HeapSnapshot` +
  epoch). `recordHeapSnapshot()` fills it; `mapResetReason` translates the IDF enum into
  the pure `ResetReason`.
- `logResetReason` reads the magic, zeroes it (consume, so an un-sampled new session does
  not inherit the prior snapshot), classifies, and on a crash formats the entry (epoch
  rendered to a UTC stamp, or `unsynced`) and appends it to SD.
- `main.cpp` calls `recordHeapSnapshot()` from the existing 5 s heartbeat.
- Bootloop protection (skip the SD write past 3 consecutive crashes) and the crash
  counter are unchanged.

## Verification

- `pio test -e native`: `test_crash_report` added (5 cases over classifyReset +
  formatCrashEntry); full suite green.
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS (built
  serially).
- On-device (follow-up, hardware): force an OOM and confirm crash.log shows a plausible
  low free-heap / small largest-block near the fault and a real timestamp.

## As built

As designed. The pure core is `core/crash_report.{h,cpp}`; the RTC recorder, the IDF enum
mapper, and the rewritten `logResetReason` are in `system_manager.cpp`; the heartbeat call
is in `main.cpp`. The log now emits the three pre-crash heap figures (or `unavailable`)
and drops the old misleading boot-time `Free Heap` / `Heap Size` / `Free PSRAM` lines.

Verification run this change:
- `pio test -e native`: 925/925 (was 920; test_crash_report adds 5 cases).
- `pio run -e cardputer -j 2`: SUCCESS. Flash 75.6% (2,525,959 B, +484 B over slice-0124's
  2,525,475 B).
- `pio run -e m5stick -j 2`: SUCCESS. RAM 21.0% (68,808 B), Flash 78.5% (2,468,815 B, +532 B
  over slice-0124's 2,468,283 B).
- Docs: index + lint green (126 documents on disk; 125 staged, excluding the untracked 0033).
- Note: a polling slip launched a second concurrent m5stick build against the first
  (against the serial-build invariant). Both were killed, the env cleaned, and m5stick
  rebuilt once in isolation for the figure above.
