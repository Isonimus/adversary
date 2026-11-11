---
id: '0152'
title: "esp_core_dump SD-rescue on boot and host-side decode for post-mortem backtraces"
type: architecture
status: accepted
date: 2026-11-11
supersedes: []
superseded_by: []
---

## Context

ADR-0126 bought the crash log "how much heap, and how fragmented, just before it died".
It explicitly did **not** buy "which function". That gap is now the blocker on a real
recurring defect.

Three BOTH-mode auto-hunt reboots recorded in `/adversary/logs/crash.log` all classify as
`WDT (other watchdog)` (i.e. the Interrupt WDT, not the Task WDT) with the flight
recorder intact and the heap healthy:

| Session wall-clock       | Uptime at crash | pre-crash heap (free / largest / min-ever) |
|--------------------------|------------------|--------------------------------------------|
| 2026-10-23 13:55 UTC     | 61 min           | 76 KB / 34 KB / 68 KB                      |
| 2026-10-23 19:02 UTC     | 6 h 8 min        | 76 KB / 34 KB / 68 KB                      |
| 2026-11-07 15:30 UTC     | 57 min           | 70.6 KB / 31 KB / 53.3 KB                  |

The remaining ~226 entries in the same log are cold power-on or brownout (RTC snapshot
`unavailable`): unrelated. These three are the signal. OOM is ruled out by the figures.
The Interrupt WDT means **a CPU core sat >600 ms in an ISR or `portENTER_CRITICAL`
section**. The suspect is radio-callback contention in BOTH mode (WiFi promiscuous RX vs
BLE scheduler, per ADR-0060 coexistence), but with no PC/task name at the crash point
that is a guess. The cost of continuing to guess is a release with a known reproducible
reboot in the flagship combined mode.

Two assumptions from ADR-0126's "kept in the ledger as its own slice" line turn out to
be wrong, and that is what lets us move:

- **No partition-table change is needed.** Both partition tables the project ships
  already carry a 64 KB `coredump` partition: the cardputer resolves the platform default
  `default_8MB.csv` (`coredump, data, coredump, 0x7F0000, 0x10000`); the m5stick's
  explicit `huge_app.csv` has `coredump, data, coredump, 0x3F0000, 0x10000`. The
  "repartition risk for already-flashed units" that ADR-0126 cited no longer applies.
- **The ESP-IDF coredump path is already enabled in both precompiled lib bundles** we use
  (`bruce_esp32-arduino-libs-20251205-131242.zip`):
  `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`,
  `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y`,
  `CONFIG_ESP_COREDUMP_CHECKSUM_CRC32=y`,
  `CONFIG_ESP_COREDUMP_CHECK_BOOT=y`, for both `esp32` and `esp32s3`. Max tasks 64,
  dedicated 1792 B panic stack on S3. Nothing to flip on the firmware build.

So the panic handler is already writing an ELF-format coredump to the `coredump`
partition on every crash the WDT handler services. We just never look at it. ADR-0126's
sentence "needs a partition table change and a host-side decode step" has been reduced
to its second half.

What remains is:

1. **Get the dump off the device.** The coredump partition survives a soft reset but is
   invisible to the operator and is overwritten by the next panic. Without rescue, every
   new crash silently erases the last one.
2. **Decode it.** `espcoredump.py info_corefile` needs the exact `firmware.elf` that was
   running when the panic happened. Our release artifact pipeline publishes `.bin` only;
   the ELF is a build intermediate and is not shipped, so a user-reported field crash is
   un-decodable against a known release.

## Decision

### 1. SD-rescue path, before the bootloop gate

On the next boot after a panic, `SystemManager::logResetReason()` already runs after
`SDManager::init()`. Add a **prelude** that, before the existing crash-count + 3-strike
bootloop guard:

- Calls `esp_core_dump_image_check()`: if it returns `ESP_OK` there is a valid ELF image
  in the `coredump` partition.
- Reads the image via `esp_core_dump_image_get(&addr, &size)` and `esp_flash_read(...)`
  in chunks (no PSRAM, 64 KB cap, 4 KB buffer).
- Writes it to `/adversary/coredumps/<utc>-<reset-reason>.elf`, using the same wall-clock
  stamp the flight-recorder log line already composes. Directory auto-created like the
  existing `/adversary/logs`.
- Appends a one-line pointer to `crash.log` (`Coredump: <path> (<n> bytes)`) so the
  existing log stays the single read-this-first file.
- Calls `esp_core_dump_image_erase()` on success so the next panic writes a fresh dump
  into a clean partition, and so a repeated same-boot read never spins.

**Order matters.** The rescue runs *before* the `rtcCrashCount > 3 -> skip SD log` gate,
so a developing bootloop still preserves at least the first dump instead of losing every
crash under the guard. The 3-strike guard continues to apply to the plaintext
`crash.log` append (unchanged behaviour); the coredump write is the one new SD op the
guard does not cover. 64 KB bounded, one write per panic: acceptable even in the
pathological case.

### 2. Host-side decode helper

Add `scripts/decode_coredump.sh` (POSIX shell, mirrors the other `scripts/` lints and
helpers; no new runtime dependencies beyond `esptool`/`espcoredump.py` already pinned
`5.2.0` in `release.yml`):

```
scripts/decode_coredump.sh <coredump.elf> [firmware.elf]
```

- If `firmware.elf` is omitted, defaults to `.pio/build/cardputer/firmware.elf` (the
  dev-build local ELF). A field-reported coredump passes the release ELF explicitly.
- Invokes `espcoredump.py info_corefile -t elf -c <coredump.elf> <firmware.elf>`.
- Writes the decoded report alongside the input (`<coredump>.decoded.txt`).

No Python wrapper, no new dependency file: `espcoredump.py` is shipped with the pinned
`esptool==5.2.0` and is on `PATH` wherever a developer has the project environment
installed.

### 3. Ship the ELF with each release

`release.yml` currently uploads `firmware.bin` (app-only) and the merged factory image.
Add `firmware.elf` from the same `.pio/build/cardputer_release/` directory to the GitHub
Release assets, named `adversary-cardputer-${version}-firmware.elf`, and extend
`SHA256SUMS.txt` to cover it. ELF is ~5-10 MB and carries DWARF symbols: it is the only
way to decode a user-reported field coredump against a known release. Pages/web-flasher
is unchanged (it serves the factory `.bin`).

The ELF is a developer artifact, not operator-facing. The release notes already list
developer files; adding a one-line "for coredump decode" callout is enough.

### 4. On-device confirmation protocol

Before merging, verify on hardware once:

- Force a known panic via a debug menu entry that dereferences a null pointer (gated by
  `-DDEV_DEBUG_PANIC=1`, not shipped). Reboot.
- Confirm `/adversary/coredumps/<ts>-PANIC.elf` exists and is non-empty.
- Confirm `crash.log` carries the new `Coredump:` pointer line.
- Confirm a second forced panic the same session rewrites, not appends, the flash image
  (new file on next boot, old one still on SD).
- Run `scripts/decode_coredump.sh` against the rescued file and the local ELF; expect a
  backtrace that names the panic-forcing function.

Once that path is green, remove the debug menu entry (or keep it behind the dev flag)
and ship. The BOTH-mode auto-hunt remains the real test: the next recurrence should drop
a decodable coredump that names the ISR or critical section, resolving the Int-WDT
investigation this ADR exists to unblock.

## Consequences

- **Backtrace available for the recurring BOTH-mode Int-WDT** and for any future panic on
  a deployed unit without a serial tether. The three recorded events become decodable
  the next time one happens; retroactive decode of the already-captured ones is not
  possible (their flash coredump was overwritten by subsequent panics).
- **64 KB per dump is enough** for our task count (we never approach 32 live tasks, let
  alone the configured 64 max, and stack frames are shallow). A truncated dump would
  still decode the top of each task's stack, which is usually the panic point.
- **Coredump ELF is useless without a matching firmware.elf.** A release built from a
  dirty tree, or a coredump decoded against the wrong release ELF, gives wrong line
  numbers silently. Strict pinning: the rescue filename includes the reset-reason
  classifier but *not* a build hash; operators must report which release version was
  running. This is a known footgun of every embedded post-mortem workflow and is
  mitigated, not solved: operators are the only developer-adjacent users and the version
  is shown on the About screen.
- **Rescue adds one SD write and one flash erase per crash boot.** Measured esp32-s3
  flash-erase on a 64 KB partition is ~40 ms; SD write of a 10-30 KB file is
  bounded by the existing SD init cost already paid. Boot-time regression on a crash
  boot is a non-issue; clean-boot path is unchanged.
- **Bootloop interaction** is deliberately asymmetric: the existing 3-strike guard still
  stops `crash.log` appends after four consecutive crashes, but the coredump rescue runs
  unconditionally (bounded by the 64 KB image, a single file per boot, erased on write).
  The rationale is that the coredump is the one artifact that lets us diagnose a
  bootloop at all; silencing it under the guard would defeat the point. If in practice
  this turns out to hammer the SD during a tight brownout bootloop, gate it on
  `rtcCrashCount <= 1` and record that as an ADR amendment.
- **No regression test for the device shell**, same shape as ADR-0126: the IDF-side
  write is driven by the panic handler and cannot be invoked from the host. The thin
  rescue routine's pure parts (filename composition, coredump-image validity bit) move
  to `core/crash_report` and get host-tested; the SD/flash I/O is device-only.
- **Keep-not-promote** per ADR-0022: on-device diagnostics plumbing, bus-factor 1,
  partition and flash-layout knowledge required. Not a useful outside-contributor task.
