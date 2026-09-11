#!/bin/bash
# Deploy the dashboard web assets to a Cardputer SD card.
#
# The firmware serves the dashboard straight off the card from
# /adversary/dashboard/ (see src/modules/server/server_manager.cpp). The source
# of truth lives in the repo under dashboard_dev/, and nothing syncs the two: an
# edit to dashboard_dev/ has no effect on the device until the files are copied
# onto the card. This script performs that copy and verifies it, so a truncated
# or 0-byte write (which renders as a blank dashboard) fails loudly here instead
# of silently on the device.
#
# Usage: scripts/deploy_dashboard.sh <sd_mount_path>
#   <sd_mount_path>  Where the SD card is mounted on this host, e.g.
#                    /media/$USER/CARDPUTER or /run/media/$USER/ADVERSARY.

set -euo pipefail

# Resolve paths relative to this script so the deploy works from any CWD and on
# any machine (no hardcoded absolute paths).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
SRC_DIR="$REPO_ROOT/dashboard_dev"

DASHBOARD_SUBPATH="adversary/dashboard"

# Portable file size in bytes (GNU stat on Linux, BSD stat on macOS).
file_size() {
    stat -c%s "$1" 2>/dev/null || stat -f%z "$1"
}

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <sd_mount_path>" >&2
    echo "  e.g. $0 /media/$USER/CARDPUTER" >&2
    exit 2
fi

SD_MOUNT="$1"

if [ ! -d "$SD_MOUNT" ]; then
    echo "Error: SD mount path is not a directory: $SD_MOUNT" >&2
    echo "  Insert the card and pass its mount point (see 'lsblk' / 'df -h')." >&2
    exit 1
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "Error: dashboard source not found at $SRC_DIR" >&2
    exit 1
fi

DEST_DIR="$SD_MOUNT/$DASHBOARD_SUBPATH"

echo "============================================"
echo "Deploying dashboard"
echo "  from: $SRC_DIR"
echo "    to: $DEST_DIR"
echo "============================================"

mkdir -p "$DEST_DIR"

# Copy every asset except the .gitkeep placeholder, preserving subdirectories
# (so a future dashboard_dev/lib/ is deployed too). Verify each destination file
# exists and its byte size matches the source before reporting success.
copied=0
while IFS= read -r -d '' src; do
    rel="${src#"$SRC_DIR"/}"
    dest="$DEST_DIR/$rel"

    mkdir -p "$(dirname "$dest")"
    cp "$src" "$dest"

    src_size="$(file_size "$src")"
    dest_size="$(file_size "$dest")"

    if [ ! -s "$dest" ]; then
        echo "  FAIL: $rel copied as empty (0 bytes) on the card" >&2
        exit 1
    fi
    if [ "$src_size" != "$dest_size" ]; then
        echo "  FAIL: $rel size mismatch (src ${src_size}B, dest ${dest_size}B)" >&2
        exit 1
    fi

    printf "  OK: %-16s %s bytes\n" "$rel" "$dest_size"
    copied=$((copied + 1))
done < <(find "$SRC_DIR" -type f ! -name '.gitkeep' -print0)

if [ "$copied" -eq 0 ]; then
    echo "Error: no dashboard assets found to deploy in $SRC_DIR" >&2
    exit 1
fi

# Flush to the removable card so it is safe to eject and the device sees a
# complete copy rather than a partially-written one.
sync

echo "============================================"
echo "Done. Deployed $copied file(s). Safe to eject the card."
echo "============================================"
