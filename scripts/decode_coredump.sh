#!/bin/bash
# Decode an esp_core_dump ELF rescued from an Adversary unit's SD card (ADR-0152).
#
# The firmware writes the ELF image the panic handler produced to
# /adversary/coredumps/<stamp>-<REASON>.elf on the next boot, then erases the flash
# partition. To get a backtrace, feed the file through espcoredump.py info_corefile
# against the firmware.elf that was running when it crashed.
#
# Usage:
#   scripts/decode_coredump.sh <coredump.elf> [firmware.elf]
#
# When firmware.elf is omitted, defaults to .pio/build/cardputer/firmware.elf, i.e.
# the local developer build. For a field report against a released version, pass the
# matching adversary-cardputer-<version>-firmware.elf downloaded from the Release.
# Writes the decoded report to <coredump>.decoded.txt alongside the input.

set -euo pipefail

coredump="${1:-}"
firmware="${2:-.pio/build/cardputer/firmware.elf}"

if [[ -z "${coredump}" ]]; then
    echo "usage: $0 <coredump.elf> [firmware.elf]" >&2
    exit 2
fi

if [[ ! -f "${coredump}" ]]; then
    echo "ERROR: coredump not found: ${coredump}" >&2
    exit 1
fi

if [[ ! -f "${firmware}" ]]; then
    echo "ERROR: firmware ELF not found: ${firmware}" >&2
    echo "  Build the local env ('pio run -e cardputer') or pass the matching" >&2
    echo "  adversary-cardputer-<version>-firmware.elf from a GitHub Release." >&2
    exit 1
fi

# Locate the coredump decoder. The modern esptool/pioarduino installs expose it as
# `esp-coredump` (console script); the historical standalone shipped as
# `espcoredump.py`; the module form `python3 -m esp_coredump` is the always-works
# fallback when neither entry point is on PATH but the package is importable.
decoder=""
if command -v esp-coredump >/dev/null 2>&1; then
    decoder="esp-coredump"
elif command -v espcoredump.py >/dev/null 2>&1; then
    decoder="espcoredump.py"
elif python3 -c "import esp_coredump" >/dev/null 2>&1; then
    decoder="python3 -m esp_coredump"
# PlatformIO's bundled Python env has esp_coredump installed even when the host
# Python does not; fall through to it when the pioarduino penv exists.
elif [[ -x "${HOME}/.platformio/penv/bin/esp-coredump" ]]; then
    decoder="${HOME}/.platformio/penv/bin/esp-coredump"
else
    echo "ERROR: No coredump decoder found. Install esptool (pinned in release.yml):" >&2
    echo "  pip install --upgrade esptool==5.2.0" >&2
    echo "Or run inside the PlatformIO environment that already has it." >&2
    exit 1
fi

# esp-coredump hard-codes the generic `xtensa-esp32-elf-gdb` name for every Xtensa
# target (see esp_coredump/coredump.py:get_gdb_path), so even with a matching
# xtensa-esp32s3-elf-gdb on PATH the tool prints "GDB executable not found" and
# bails. Locate whatever target GDB the PlatformIO toolchain has installed, and
# pass it in via --gdb so the tool's name lookup is bypassed.
gdb_arg=()
# Preferred locations in order: a locally-extracted modern esp-elf-gdb (bundles its
# own Python 3 and is what espressif ships for IDF v5.x), then the pioarduino
# toolchains (older crosstool-NG GDB with a libpython2.7 dependency that modern
# distros no longer ship, so this fallback only works where libpython2.7 is
# installed). Install path is just a tarball under ~/.local, no sudo required:
#   https://github.com/espressif/binutils-gdb/releases
for gdb_candidate in \
    "${HOME}/.local/esp-gdb/xtensa-esp-elf-gdb/bin/xtensa-esp32s3-elf-gdb" \
    "${HOME}/.local/esp-gdb/xtensa-esp-elf-gdb/bin/xtensa-esp32-elf-gdb" \
    "${HOME}/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gdb" \
    "${HOME}/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-gdb" \
    "${HOME}/.platformio/packages/toolchain-xtensa32/bin/xtensa-esp32-elf-gdb" \
    "${HOME}/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-gdb"; do
    if [[ -x "${gdb_candidate}" ]]; then
        gdb_arg=(--gdb "${gdb_candidate}")
        break
    fi
done

output="${coredump}.decoded.txt"
echo "Decoding ${coredump} against ${firmware}"
echo "Decoder: ${decoder}"
echo "Output:  ${output}"
echo

# -t raw: the on-SD file is the exact blob `esp_core_dump_image_get()` reports, which
# is the wrapped on-flash format (4-byte length header + ELF payload + checksum). The
# `-t elf` flag is for a pre-extracted ELF payload; stripping the wrapper on device
# adds complexity with no benefit, since the decoder parses both.
#
# If GDB fails to spawn with "libpython2.7.so.1.0: cannot open shared object file"
# the host is missing Python 2 runtime that the crosstool-NG xtensa GDB links
# against; install it (e.g. `sudo apt install libpython2.7`), or install a newer
# standalone esp-elf-gdb from Espressif and pass it via --gdb.
if ! ${decoder} info_corefile "${gdb_arg[@]}" -t raw -c "${coredump}" "${firmware}" | tee "${output}"; then
    echo >&2
    echo "Full decode failed. The crash-task backtrace can still be resolved directly:" >&2
    gdb_dir=""
    if [[ ${#gdb_arg[@]} -ge 2 ]]; then
        gdb_dir="$(dirname "${gdb_arg[1]}")/"
    fi
    echo "  ${gdb_dir}xtensa-esp32s3-elf-addr2line -pfiaCe ${firmware} <PC1> <PC2> ..." >&2
    echo "with the PCs from the Guru Meditation Backtrace line (if the crash was serial-attached)." >&2
    exit 1
fi
