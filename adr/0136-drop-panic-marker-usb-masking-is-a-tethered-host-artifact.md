---
id: '0136'
title: "Drop the panic-marker plan: USB-masked crashes are a tethered-host artifact"
type: architecture
status: accepted
date: 2026-11-01
supersedes: ['0135']
superseded_by: []
---

## Context

ADR-0135 decided to stamp an RTC marker from the panic handler so a crash that reboots as
`ESP_RST_USB` would still be recorded, and wrote in a forced-panic on-device acceptance
gate before it could be trusted. Running that gate on the real hardware (Cardputer ADV,
`/dev/ttyACM0`, 2026-11-01) overturned both the premise and the mechanism. The decision is
reversed here; the measurements are the record of why.

Two things were measured:

1. **The panic-marker mechanism cannot be built on our toolchain.** ADR-0135 planned to
   stamp the marker from the Arduino core's panic hook (`set_arduino_panic_handler`, which
   rides the core's `-Wl,--wrap=esp_panic_handler`). On device the hook never fires: with
   the handler confirmed registered (`get_arduino_panic_handler` reads it back), a forced
   `abort()` and a forced stack-canary panic both ran to reboot without the callback
   executing (an `esp_rom_printf` inside it never printed; the RTC marker was never set).
   The real `esp_panic_handler` in the precompiled `framework-arduinoespressif32-libs`
   (bruce) archive is reached directly, so the final-link `--wrap` does not intercept it,
   and the Arduino custom-panic hook built on that wrap is inert. This is exactly the
   "wrap may not fire on a precompiled archive" risk ADR-0135 flagged as its acceptance
   condition, and it failed.

2. **The blind spot is a tethered-host artifact, not a field blind spot.** A panic's own
   reset reason is `ESP_RST_PANIC` (`rst:0xc RTC_SW_CPU_RST`), measured directly, and the
   existing reset-reason classifier logs it to `crash.log` correctly. The boot only reads
   `ESP_RST_USB` when a host-side reset (a serial monitor connecting, or the USB host
   re-enumerating) toggles the reset line on the post-crash boot before `logResetReason`
   runs. An A/B on device confirmed it: with no host toggle, the post-panic boot reports
   `PANIC` and logs the crash; injecting a USB reset into that boot's window makes it
   report `USB` and the crash is masked. The 2026-11-01 reading that a panic "resets as
   `ESP_RST_USB`" was produced by a capture method that reopens the port through
   re-enumeration, which injects precisely that reset. So the original premise generalised
   a tether-only artifact into a universal blind spot.

The practical impact of the remaining, narrow masking is close to nil. When a host is
attached (the only way the masking occurs), the panic and its backtrace are already
visible live on that serial console. When the device runs standalone on battery (the field
deployment), no host toggles reset, the reset reason stays `PANIC`, and `crash.log`
captures the crash. The case ADR-0135 targeted is one where the crash is already in front
of you, and the case that matters for field forensics is already handled.

## Decision

Do not implement the panic marker. Keep the existing reset-reason classifier
(`classifyReset` + the fail-loud-on-unmapped guard from commit 28817e1) as the crash-log
trigger. Revert the ADR-0135 device scaffolding (the RTC marker, the `set_arduino_panic_handler`
registration, and the `classifyBoot`/`PanicMarker` override in `crash_report`); none of it
shipped or was committed.

No code mitigation is added for the tethered-host masking: it is low impact (the crash is
visible live on the attached console in that case) and the only mechanism that could bridge
it on a precompiled IDF is a flash core dump, which ADR-0126 already set aside on
partition-table grounds. If that trade ever changes, it reopens as its own ADR, not this one.

## Consequences

- `crash.log` behaviour is unchanged from before ADR-0135: panics are recorded in
  standalone operation, which is where the log is the only record. The RTC heap flight
  recorder (ADR-0126) is untouched.
- A crash that coincides with a host resetting the device at reboot time is still not
  written to `crash.log`, by deliberate decision. It is observable live on the attached
  serial console, so no forensic record is lost that was not already on screen.
- The ADR-0135 forced-panic acceptance gate did its job: it stopped an unbuildable,
  premise-flawed mechanism from shipping. The `crash-classification` ledger item is closed.
- If a standalone-reproducible crash ever surfaces that the reset reason genuinely cannot
  classify (not a tethered USB artifact), the flash-core-dump path (ADR-0126) is the place
  to reopen, with its partition cost weighed then.
