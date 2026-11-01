---
id: '0135'
title: "Panic-set RTC marker so USB-reset crashes are no longer invisible"
type: architecture
status: accepted
date: 2026-11-01
supersedes: []
superseded_by: []
---

## Context

`SystemManager::logResetReason()` classifies each boot's cause from
`esp_reset_reason()` and writes `/adversary/logs/crash.log` only when the cause is a
crash (ADR-0126 added the dying-heap snapshot to that entry). The classifier is pure
and host-tested (`crash_report.cpp`).

On this hardware the reset reason is not sufficient to tell a crash from a benign event.
Measured 2026-11-01: a forced stack-canary violation (and, separately, an "unhandled
debug exception") reboots the ESP32-S3 with `esp_reset_reason() == ESP_RST_USB`, the
same value the chip reports for a harmless USB host re-enumeration. `mapResetReason`
maps `ESP_RST_USB` to `ResetReason::UsbReset`, and `classifyReset(UsbReset)` returns
`isCrash = false`. So a real panic on the USB-CDC path is swallowed whole: no
`crash.log` entry, no consecutive-crash streak increment, no bootloop guard. This is
how the 2026-11-01 `gps_detect` stack-canary panic went unrecorded while it was being
diagnosed; the stack bug itself is fixed and soak-verified (commit 12239d7), but the
classification blind spot that hid it is still open.

The earlier fix (commit 28817e1) mapped the full IDF 5.x reset enum and made
`logResetReason` fail loud on an *unmapped* cause. That closes the "new enum value
buckets silently into non-crash" gap, but it does nothing here: `ESP_RST_USB` *is*
mapped, deliberately, to a non-crash. The symptom (a USB reset) is genuinely
indistinguishable, after the fact, from a benign re-enumeration.

The only way to separate the two is a signal captured at panic time, before the chip
resets. Two mechanisms can carry such a signal across the reset:

- **A full core dump to flash** (`esp_core_dump`). ADR-0126 already weighed and set this
  aside: it needs a partition-table change, and we ship the stock `huge_app.csv` layout
  (3 MB app, no OTA, no coredump partition), so adopting it repartitions every
  already-flashed unit. It also needs an off-device decode step and is not legible to
  the operator on the device. That reasoning has not changed; it remains a larger,
  separable piece of work.
- **A one-word marker in RTC memory**, set from the panic handler. RTC `NOINIT` RAM
  already survives a soft reset untouched (ADR-0126 relies on exactly this for the heap
  flight recorder). Writing a single sentinel word is panic-safe in a way the heap APIs
  and SD I/O are not (the two constraints ADR-0126 documents): it takes no lock and
  touches no peripheral.

## Decision

Set an RTC-memory **panic marker** from a wrapped panic handler, and let that marker
override reset-reason classification on the next boot.

- Add an `RTC_NOINIT_ATTR` marker (a magic sentinel plus a small cause code) alongside
  the existing flight recorder in `system_manager.cpp`.
- Wrap the IDF panic handler with the linker's `--wrap` option: `__wrap_esp_panic_handler`
  stamps the marker, then tail-calls `__real_esp_panic_handler` so the normal panic flow
  (register dump, reboot) is unchanged. The wrap flag is added in `platformio.ini`
  build flags for the two device envs. The handler does the minimum: write the sentinel
  word, nothing else.
- On boot, `logResetReason` reads the marker **before** trusting `esp_reset_reason()`.
  When the marker is set, the boot is classified as a crash regardless of the reset
  reason (so an `ESP_RST_USB` that follows a panic is logged), the cause code is folded
  into the `crash.log` label, and the marker is then cleared so a subsequent clean boot
  is not misread as a crash.
- Keep the classification override **pure and host-tested**: the decision rule ("a set
  panic marker forces isCrash true, and names the cause") lives in `crash_report.cpp`
  next to `classifyReset`, taking the marker state as an explicit input. The device
  shell only reads RTC memory and the IDF enum and feeds them in. This matches the
  ADR-0126 split (pure core, thin device shell) and keeps the new logic in the native
  test suite.

The reset-reason classifier and the "fail loud on unmapped cause" guard both stay; the
marker is an additional, higher-priority input, not a replacement.

### Acceptance condition (must hold before the fix is trusted)

The one unproven assumption is that a stack-canary / debug-exception panic actually
reaches `esp_panic_handler`, and that `--wrap` redirects that call when the handler
lives in a precompiled IDF archive. Both are expected to hold (the exception vectors
reference `esp_panic_handler` as an external symbol resolved at final link), but this
**must be proven on-device with a forced panic** before the fix is considered done: force
a stack-canary violation, confirm the next boot writes a `crash.log` entry and increments
the streak even though `esp_reset_reason()` reports `ESP_RST_USB`. Host tests cannot cover
the wrap-and-reset path; this is a hardware-verify gate, tracked in `LEDGER.md`, not
promoted to an issue.

## Consequences

- A panic on the USB-CDC reset path is recorded like any other crash: `crash.log` entry,
  dying-heap snapshot (reused from ADR-0126), streak increment, and bootloop protection
  all apply. The blind spot that hid the `gps_detect` panic is closed at the root, not
  papered over by reclassifying `ESP_RST_USB` as a crash wholesale (which would log every
  benign re-enumeration as a crash).
- Small, bounded cost: one RTC word, one wrapped symbol, a few lines in the panic path
  that must stay lock-free and peripheral-free. No partition change, no flash layout
  risk to flashed units, no host-side decode tooling.
- If the acceptance condition fails on hardware (the wrap does not fire on the
  debug-exception path), this decision does not ship as-is; the fallback is the heavier
  `esp_core_dump` path, which would then need its own ADR to reopen the partition
  question ADR-0126 closed.
- The native test suite gains cases for the override rule; the device-side wrap and the
  RTC marker's survival across a real reset are covered only by the on-device gate above.
