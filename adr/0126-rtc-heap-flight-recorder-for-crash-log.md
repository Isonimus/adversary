---
id: '0126'
title: "RTC heap flight recorder so the crash log captures the dying heap"
type: architecture
status: accepted
date: 2026-10-26
supersedes: []
superseded_by: []
---

## Context

`SystemManager::logResetReason()` writes `/adversary/logs/crash.log` on the *next* boot,
right after SD mount, sampling `ESP.getFreeHeap()` and friends at that moment. The reset
reason it records is real, but the heap figures describe a fresh boot, not the crash:
every entry reads `Boot Time ~1395 ms` with a healthy 149-176 KB free.

This was found twice while diagnosing out-of-memory reboots (2026-10-15 wardriving
BOTH-mode OOM; re-confirmed 2026-10-22 on the phase-2 OOM). Both times the log confirmed
*that* we panicked but said nothing about *why*: no free-heap-at-crash, no largest-free
block (fragmentation). The diagnosis had to come from the heap CSV and code instead.

Two constraints rule out the obvious "just read the heap when it crashes":

- **No heap API is panic-safe.** `heap_caps_get_free_size` / `..._largest_free_block`
  take the heap mutex; calling them from the panic handler can deadlock.
- **No SD I/O in the panic path.** Filesystem writes there hang or corrupt the card.

A full `esp_core_dump`-to-flash path would give a backtrace, but it needs a partition
table change (a repartition risk for already-flashed units) and a host-side decode step.
That is a larger, separable piece of work.

## Decision

Keep a small **flight recorder** in RTC memory and log *that* on the next boot.

- A `RTC_NOINIT_ATTR` struct holds the last heap sample (`freeHeap`,
  `largestFreeBlock`, `minFreeHeap`, `uptimeMs`) plus a wall-clock epoch and a magic
  sentinel. RTC RAM survives a soft reset (PANIC/WDT) untouched, which is exactly the
  OOM case; `NOINIT` keeps the C runtime from clearing it.
- The main loop samples it on the existing 5 s heartbeat (`SystemManager::recordHeapSnapshot`).
- On the next boot, `logResetReason` reads the sentinel to decide whether the snapshot
  survived, then writes the pre-crash heap (or, if RTC did not survive, the literal word
  `unavailable`) stamped with the sample's wall-clock time.
- The sentinel is **consumed** at boot: `logResetReason` reads validity, then zeroes the
  magic, so a crash early in the new session (before it has sampled) reports
  `unavailable` instead of the previous session's stale numbers.
- The reset-reason classification and the entry text are a **pure core**
  (`core/crash_report.{h,cpp}`), host-tested natively; the device shell only gathers
  inputs (`esp_reset_reason`, the RTC struct, the clock) and does the SD write. The
  shell is device-only, so the classifier is where a regression would otherwise hide
  untested.

The log drops the misleading boot-time `Free Heap` / `Heap Size` / `Free PSRAM` lines
(the device has no PSRAM, per the no-PSRAM invariant) in favour of the three pre-crash
figures that the two real diagnoses needed.

## Consequences

- The crash log now answers "how much heap, and how fragmented, just before it died",
  which is what both OOM investigations lacked.
- The sample is up to ~5 s stale (it rides the heartbeat, not a dedicated timer). For a
  gradual OOM, which both observed cases were, that granularity captures the decay; a
  sudden single-allocation spike inside one heartbeat window would be missed. Accepted:
  a tighter cadence buys little against the heap-mutex cost and the gradual failure mode.
- RTC RAM is lost on a true power cycle and (usually) a brownout. Those are reported as
  `unavailable`, not guessed: a brownout is a power fault where heap is irrelevant
  anyway.
- No backtrace / allocation site. That remains the job of an `esp_core_dump`-to-flash
  path, which is **kept in the ledger as its own slice** (partition-table change,
  decode step), sequenced separately per ADR-0022. This ADR is the lightweight fix, not
  a replacement for it.
- Keep-not-promote per ADR-0022: on-device diagnostics internals, bus-factor 1.
