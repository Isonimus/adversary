#!/bin/bash
# Weaken ieee80211_raw_frame_sanity_check symbol in libnet80211.a
# This allows our override to work, enabling deauth/disassoc frame transmission
# 
# Supports both ESP32 (M5Stick) and ESP32-S3 (Cardputer)
# Run this after PlatformIO downloads/updates the framework-arduinoespressif32-libs package

set -e

LIBS_BASE="$HOME/.platformio/packages/framework-arduinoespressif32-libs"

# Resolve objcopy without hardcoding an absolute toolchain path (package names change
# across PlatformIO toolchain updates, e.g. the unified toolchain-xtensa-esp-elf split
# into per-chip toolchain-xtensa-esp32s3). Precedence:
#   1. $OBJCOPY exported by the build hook (weaken_deauth_pre.py), derived from SCons $CC
#      — the exact toolchain this build uses.
#   2. PATH lookup, for standalone manual runs.
# objcopy --weaken-symbol edits the archive symbol table only, so one xtensa objcopy
# weakens both esp32 and esp32s3 libs regardless of which chip's toolchain it came from.
resolve_objcopy() {
    if [ -n "$OBJCOPY" ] && [ -x "$OBJCOPY" ]; then
        echo "$OBJCOPY"; return 0
    fi
    local FROM_PATH
    FROM_PATH=$(command -v xtensa-esp32s3-elf-objcopy || command -v xtensa-esp-elf-objcopy || true)
    if [ -n "$FROM_PATH" ]; then
        echo "$FROM_PATH"; return 0
    fi
    return 1
}

# Function to weaken symbol for a specific chip
weaken_for_chip() {
    local CHIP=$1
    local LIBPATH="$LIBS_BASE/$CHIP/lib/libnet80211.a"

    echo "============================================"
    echo "Processing $CHIP..."
    echo "============================================"
    
    if [ ! -f "$LIBPATH" ]; then
        echo "  Warning: libnet80211.a not found at $LIBPATH"
        echo "  Skipping $CHIP (run 'pio run' first to download dependencies)"
        return 0
    fi

    # Check current symbol state
    CURRENT=$(nm "$LIBPATH" 2>/dev/null | grep "ieee80211_raw_frame_sanity_check" | awk '{print $2}')

    if [ "$CURRENT" = "W" ]; then
        echo "  Symbol is already weak (W) - no changes needed"
        return 0
    fi

    echo "  Current symbol state: $CURRENT (expected T for strong)"
    echo "  Weakening ieee80211_raw_frame_sanity_check symbol..."

    "$OBJCOPY_BIN" --weaken-symbol=ieee80211_raw_frame_sanity_check "$LIBPATH"

    # Verify
    NEW=$(nm "$LIBPATH" 2>/dev/null | grep "ieee80211_raw_frame_sanity_check" | awk '{print $2}')

    if [ "$NEW" = "W" ]; then
        echo "  Success! Symbol is now weak (W)"
        echo "  Deauth bypass override will now work on $CHIP"
    else
        echo "  Error: Symbol state is '$NEW', expected 'W'"
        return 1
    fi
}

echo "Weakening deauth symbols for ESP32 platforms..."
echo ""

# Resolve objcopy once, up front — fail loud before touching any archive if none exists.
OBJCOPY_BIN=$(resolve_objcopy) || {
    echo "Error: no xtensa objcopy found — set \$OBJCOPY or add xtensa-esp32s3-elf-objcopy to PATH"
    exit 1
}
echo "Using objcopy: $OBJCOPY_BIN"
echo ""

# Process ESP32-S3 (Cardputer)
weaken_for_chip "esp32s3"

echo ""

# Process ESP32 (M5Stick and other PICO devices)
weaken_for_chip "esp32"

echo ""
echo "============================================"
echo "Done! Rebuild your project to apply changes."
echo "============================================"
