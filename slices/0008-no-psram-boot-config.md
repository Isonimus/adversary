---
id: '0008'
title: "Stop the failed octal-PSRAM boot init on a chip-less board"
type: slice
status: accepted
date: 2026-09-06
supersedes: []
superseded_by: []
---

## Goal

Stop the noisy, failed **octal-PSRAM initialisation at boot** on a device that has **no
PSRAM chip at all**. Boot currently logs `octal_psram: PSRAM chip is not connected, or
wrong PSRAM line mode` and reports `PSRAM: 0`, because `platformio.ini` forces
`board_build.arduino.memory_type = qio_opi` (octal-PSRAM SDK libs + bootloader) onto the
StampS3-based Cardputer. Neither the StampS3-based original Cardputer nor the deployment
device (Cardputer ADV) has PSRAM — this has been independently re-derived at least twice
because the misleading boot error and stale docs invite the question "does this board have
PSRAM?" every time.

This slice makes the build config **honest**: remove the octal override so the board falls
to its own default (`qio_qspi` — quad-PSRAM libs whose probe fails quietly on a chip-less
board), guard against the override being re-added, and correct the two stale PSRAM claims
in `CLAUDE.md` that seeded the confusion. The invariant "this hardware has no PSRAM" is
recorded in the repo so it stops being re-derived.

**This is a correctness / clean-boot / anti-re-derivation fix, not a memory win.** There is
no PSRAM to reclaim: the ~205 KB internal SRAM is the whole budget before and after, and
upload/TLS headroom already comes from the canvas purge, not external RAM. `BOARD_HAS_PSRAM`
is not defined in this build, so the one PSRAM-conditional line (`main.cpp` `setPsram(true)`)
was already skipped — the canvas already lived in internal RAM.

## Definition of Done

- **Given** the deployment device (Cardputer ADV, no PSRAM chip)
- **When** it boots with the octal override removed
- **Then** the boot log shows **no** `octal_psram ... wrong line mode` failure, the device
  reaches the idle menu normally, and boot does not abort or loop (the quad default probe
  fails quietly and continues on internal SRAM)

- **Given** the corrected `platformio.ini`
- **When** the cardputer firmware is built
- **Then** it compiles, links, and flashes with no regression — QIO flash mode is unchanged
  (the change only swaps which PSRAM lib variant is linked), and RAM/Flash usage is
  comparable to before

- **Given** no application code relies on external RAM (`BOARD_HAS_PSRAM` undefined)
- **When** the firmware runs after the PSRAM-mode change
- **Then** existing behaviour is unchanged — the canvas renders and a WPA-SEC/TLS upload
  (the canvas-purge headroom path) still completes: the change touches only the boot-time
  PSRAM probe, not any runtime allocation the firmware actually makes

- **Given** a future contributor reading `platformio.ini` or `CLAUDE.md`
- **When** they consider PSRAM / `ps_malloc` / a `memory_type` override
- **Then** the no-PSRAM invariant is stated and a guard comment names `qio_opi` as the
  specific value **not** to set — so the fix is not silently reverted next time

## Design

### Root cause

`platformio.ini` sets `board_build.arduino.memory_type = qio_opi`. The pioarduino builder
(`_get_board_memory_type`) reads that override in preference to the board default; `qio_opi`
selects the **octal-PSRAM** precompiled `libesp_psram.a` + second-stage bootloader. On a
board with no PSRAM chip, the octal init in that bootloader fails and logs the "wrong PSRAM
line mode" error before continuing on internal SRAM.

The StampS3 board definition (`m5stack-stamps3.json`) is **already correct** for a no-PSRAM
module: it declares no `memory_type`, no `BOARD_HAS_PSRAM`, and `maximum_ram_size: 327680`
(320 KB internal only). The bug is entirely the override we added on top of a correct board
def — not the board def, and not a missing `cardputer_adv` env.

### The fix

Delete the `qio_opi` override. With no override, `_get_board_memory_type` resolves the board
default `"<flash_mode>_<psram_type>"` = `qio_qspi` (StampS3 JSON: `flash_mode: qio`,
`psram_type` unset → defaults to `qspi`). QIO flash is preserved (unchanged from today's
`board_build.flash_mode = qio` and the board JSON); only the PSRAM lib variant changes from
octal to quad. The quad probe on a chip-less board fails **quietly** and boot continues —
removing the misleading octal error.

A guard comment replaces the deleted line so the *why* survives in the file a reader edits,
naming `qio_opi` as the value not to re-add. This is the KISS choice over the LEDGER's
"add a `cardputer_adv` env": the board def already models no-PSRAM, one env serves the
device, and the ADV's only real difference (the TCA8418 I2C keyboard) is already handled at
runtime via `M5.getBoard()` — a second env would be speculative generality.

### Why the precompiled libs bound the fix

We are pinned to the bruce/bmorcelli precompiled Arduino libs (they carry the weak
`ieee80211_raw_frame_sanity_check` symbol the deauth TX path needs). Those libs ship only
the five flash×PSRAM combos (`dio_opi`, `dio_qspi`, `opi_opi`, `qio_opi`, `qio_qspi`) — all
with `CONFIG_SPIRAM=y`; there is **no** PSRAM-disabled variant, and we cannot rebuild the
baked-in sdkconfig. A perfectly PSRAM-free boot is therefore not on the table without giving
up the deauth patch. `qio_qspi` is the pragmatic honest choice: it stops the loud octal
failure and matches the board's own default. (If a no-SPIRAM precompiled variant ever ships,
adopting it is a follow-up, not this slice.)

### Recording the invariant (the anti-re-derivation goal)

Two stale claims in `CLAUDE.md` seeded the repeated misdiagnosis and are corrected here:
- The illustrative `[env:cardputer]` config listed `-DBOARD_HAS_PSRAM` — removed (the real
  build never defines it, and copying it would wrongly route the canvas into non-existent
  PSRAM).
- The ESP32 pitfall "**PSRAM**: Use `ps_malloc()` for large buffers on Cardputer" is
  replaced with the **No PSRAM** invariant: the deployment device has no PSRAM chip,
  `ps_malloc` does not buy external RAM, `qio_opi` must not be set, and the ~205 KB internal
  SRAM is the whole budget (upload/TLS headroom via the canvas purge).

### Why no native unit test

Nothing testable changes in code — this slice edits build configuration and documentation
only. There is no logic seam to exercise on the host; the observable behaviour is the boot
sequence, which is a hardware/bootloader boundary (CLAUDE.md: HAL/boot is verified on-device,
not natively). The regression guard is therefore the on-device boot log plus the desk build,
recorded in `## As built` — not a fabricated unit test that would assert nothing real.

## Verification

- **Desk build** (`pio run -e cardputer`): compiles + links clean on `qio_qspi`; capture
  RAM/Flash figures and confirm they are comparable to the `qio_opi` build (no regression
  from the lib-variant swap).
- **On-device** (Cardputer ADV, serial boot capture — the boundary that cannot run natively),
  recorded in `## As built`:
  - Flash the corrected build and read the boot log: assert **no** `octal_psram ... wrong
    line mode` line, the device reaches the idle menu, and boot does not abort/loop.
  - Measure the boot-time delta vs. the prior octal-failure build (any reduction is a bonus;
    the acceptance criterion is the clean boot, not a specific saving).
  - Confirm `PSRAM: 0` is still reported (hardware reality) but now as a quiet quad probe,
    not a configuration error.
  - Functional no-regression smoke test: open a canvas-rendering screen and run one
    WPA-SEC/TLS upload (the canvas-purge headroom path) — both must work as before.

## As built

Shipped as designed: the `board_build.arduino.memory_type = qio_opi` override was removed
from `platformio.ini` (replaced by a guard comment naming `qio_opi` as the value not to
re-add), and the two stale PSRAM claims in `CLAUDE.md` were corrected — the illustrative
`-DBOARD_HAS_PSRAM` build flag dropped, and the "use `ps_malloc()`" pitfall replaced with
the **No PSRAM (invariant)** note. No source changed: the one PSRAM-conditional line
(`main.cpp` `setPsram(true)`) is under `#ifdef BOARD_HAS_PSRAM`, which is not defined, so it
was already inert.

**Mechanism proof (desk, binary-level).** With the override gone the build resolves the
board default `qio_qspi`, which links the **quad** `libesp_psram.a` + bootloader instead of
the octal ones. The exact error string the LEDGER observed — `PSRAM chip is not connected,
or wrong PSRAM line mode` — is present in `qio_opi/libesp_psram.a` (6 octal-related matches)
but is **not linked into our `qio_qspi` firmware.elf** (`grep -c` = 0 for both "wrong PSRAM
line mode" and "PSRAM chip is not connected"). The linker drops the unreferenced octal
object; what is linked is the quad impl (`esp_psram_impl_ap_quad.c`). The octal init path and
its failure message are therefore compiled out — the error cannot be emitted, by
construction.

**On-device (Cardputer ADV, 2026-09-06), serial boot capture over `/dev/ttyACM0`:**
- Clean boot to the idle menu — `Initialization complete. Entering main loop.` at **3.86 s**;
  no abort, no boot loop. M5GFX auto-detected `board_M5CardputerADV` (confirms the deployment
  device).
- PSRAM handled quietly and correctly: `[Init] Created global canvas 240x135 (Internal:
  212416, PSRAM: 0)` — the canvas lives in internal SRAM and `PSRAM: 0` is reported as plain
  fact, not a configuration error.
- No functional regression: SD mounted (SDHC, 214 records hydrated from manifest), cap
  resolved `None` (CC1101/NRF24 absent → GNSS cap), GPS detected (`AT6668 on Cap`), GPS time
  sync + live GGA fix (13 sats, hdop 1.2), heap heartbeat 155 KB free. The canvas-render and
  GPS paths — the runtime allocations that actually matter — all work as before.

**Build:** `pio run -e cardputer` SUCCESS on `qio_qspi` — RAM **26.3 %** (86,232 B) and Flash
**71.7 %** (2,395,999 B), both marginally *lower* than the prior `qio_opi` build (26.5 % /
72.4 %) because the quad libs are slightly smaller; QIO flash mode unchanged. Native suite
unaffected (no source touched) and re-run green.

**Measurement boundary (honest).** The pre-app bootloader / ESP-IDF startup console does not
reach `/dev/ttyACM0`: the app's USB-CDC re-enumerates across a reset, and no
USB-Serial-JTAG console is present on this host (only `ttyACM0`). The first captured line is
the app banner at 0.396 s — no ROM/bootloader lines appear at all. So the elimination of the
octal path is established by the linked-binary evidence above plus the clean app boot, **not**
by re-observing the bootloader console directly. This is a stronger guarantee than a console
grep would have been: the failing code is absent from the image, not merely silent.

