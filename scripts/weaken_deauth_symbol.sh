#!/bin/bash
# Weaken ieee80211_raw_frame_sanity_check symbol in libnet80211.a
# This allows our override to work, enabling deauth/disassoc frame transmission
# 
# Supports both ESP32 (M5Stick) and ESP32-S3 (Cardputer)
# Run this after PlatformIO downloads/updates the framework-arduinoespressif32-libs package

set -e

LIBS_BASE="$HOME/.platformio/packages/framework-arduinoespressif32-libs"
TOOLCHAIN_BASE="$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"

# Function to weaken symbol for a specific chip
weaken_for_chip() {
    local CHIP=$1
    local LIBPATH="$LIBS_BASE/$CHIP/lib/libnet80211.a"
    local OBJCOPY="$TOOLCHAIN_BASE/xtensa-esp-elf-objcopy"
    
    echo "============================================"
    echo "Processing $CHIP..."
    echo "============================================"
    
    if [ ! -f "$LIBPATH" ]; then
        echo "  Warning: libnet80211.a not found at $LIBPATH"
        echo "  Skipping $CHIP (run 'pio run' first to download dependencies)"
        return 0
    fi

    if [ ! -f "$OBJCOPY" ]; then
        echo "  Error: xtensa objcopy not found at $OBJCOPY"
        return 1
    fi

    # Check current symbol state
    CURRENT=$(nm "$LIBPATH" 2>/dev/null | grep "ieee80211_raw_frame_sanity_check" | awk '{print $2}')

    if [ "$CURRENT" = "W" ]; then
        echo "  Symbol is already weak (W) - no changes needed"
        return 0
    fi

    echo "  Current symbol state: $CURRENT (expected T for strong)"
    echo "  Weakening ieee80211_raw_frame_sanity_check symbol..."

    "$OBJCOPY" --weaken-symbol=ieee80211_raw_frame_sanity_check "$LIBPATH"

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

# Process ESP32-S3 (Cardputer)
weaken_for_chip "esp32s3"

echo ""

# Process ESP32 (M5Stick and other PICO devices)
weaken_for_chip "esp32"

echo ""
echo "============================================"
echo "Done! Rebuild your project to apply changes."
echo "============================================"
