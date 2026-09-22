#!/bin/bash
# Convert Fn+S device screenshots (BMP) into README-ready PNGs.
#
# The firmware writes screenshots as 24-bit BMPs at the panel's native 240x135
# to /adversary/screenshots/shot_NNN.bmp (see src/utils/screenshot.cpp). Those
# are unsuitable for the README as-is: BMP is ~97 KB each and some Markdown
# renderers will not display it, and 240x135 is too small to read on GitHub.
#
# This script batch-converts a directory of BMPs to PNG (lossless, ~10-20x
# smaller) and upscales 3x with nearest-neighbour ("point") sampling, so the
# hard pixel edges stay crisp instead of blurring the way a smooth resize would.
# The basename is preserved (shot_000.bmp -> shot_000.png); rename the PNGs to
# their semantic README names (scanner.png, evil-twin.png, ...) afterwards.
#
# Usage: scripts/screenshots-to-png.sh <src_dir> [dest_dir]
#   <src_dir>   Directory holding the .bmp files pulled off the SD card.
#   [dest_dir]  Where to write the .png files. Defaults to docs/screenshots/.

set -euo pipefail

# Nearest-neighbour upscale factor. 3x turns 240x135 into 720x405 — large enough
# to read on GitHub while integer-scaling keeps every source pixel a clean block.
readonly SCALE_PERCENT=300

# Resolve the default dest relative to this script so it works from any CWD and
# on any machine (no hardcoded absolute paths).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
DEFAULT_DEST="$REPO_ROOT/docs/screenshots"

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 <src_dir> [dest_dir]" >&2
    echo "  e.g. $0 ~/cardputer-shots" >&2
    echo "  dest_dir defaults to docs/screenshots/" >&2
    exit 2
fi

SRC_DIR="$1"
DEST_DIR="${2:-$DEFAULT_DEST}"

# Pick the ImageMagick entrypoint: v7 ships 'magick', v6 only 'convert'. Fail
# loudly with an install hint if neither is on PATH rather than emitting nothing.
if command -v magick >/dev/null 2>&1; then
    CONVERT=(magick)
elif command -v convert >/dev/null 2>&1; then
    CONVERT=(convert)
else
    echo "Error: ImageMagick not found (need 'magick' or 'convert' on PATH)." >&2
    echo "  Install it, e.g.: sudo apt install imagemagick" >&2
    exit 1
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "Error: source directory not found: $SRC_DIR" >&2
    exit 1
fi

mkdir -p "$DEST_DIR"

echo "============================================"
echo "Converting screenshots (BMP -> PNG, ${SCALE_PERCENT}% point-scaled)"
echo "  from: $SRC_DIR"
echo "    to: $DEST_DIR"
echo "============================================"

# Match .bmp case-insensitively so shots named .BMP are picked up too.
converted=0
while IFS= read -r -d '' src; do
    base="$(basename "$src")"
    dest="$DEST_DIR/${base%.*}.png"

    "${CONVERT[@]}" "$src" -filter point -resize "${SCALE_PERCENT}%" "$dest"

    if [ ! -s "$dest" ]; then
        echo "  FAIL: $base produced an empty PNG at $dest" >&2
        exit 1
    fi

    printf "  OK: %-18s -> %s\n" "$base" "${base%.*}.png"
    converted=$((converted + 1))
done < <(find "$SRC_DIR" -maxdepth 1 -type f -iname '*.bmp' -print0)

if [ "$converted" -eq 0 ]; then
    echo "Error: no .bmp files found in $SRC_DIR" >&2
    exit 1
fi

echo "============================================"
echo "Done. Converted $converted file(s) into $DEST_DIR"
echo "  Next: rename to semantic names (scanner.png, ...) and wire the README."
echo "============================================"
