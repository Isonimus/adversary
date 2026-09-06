# Spike: 8-bit global canvas to halve DRAM pressure

**Branch:** `spike/8bit-canvas-palette` (off `feature/hid-menu`)
**Status:** first cut built (RGB332), NOT yet visually verified on-device.
**Goal:** stop the 64.8 KB canvas from competing with the ~62 KB TLS record
buffers in internal DRAM, so cloud syncs survive a fragmented post-Auto-Hunt
heap without a reboot — ideally retiring the canvas-purge dance entirely.

## Why this exists

- A TLS handshake costs ~62 KB (16 KB IN + 16 KB OUT record buffers, baked
  into the precompiled mbedTLS lib — not shrinkable). See commit `e2718cd`.
- This board (StampS3) has **no usable PSRAM** — boot log confirms
  `[Init] Created global canvas 240x135 (Internal: 177028, PSRAM: 0)`. So
  `setPsram(true)` silently falls back to internal DRAM and the canvas fights
  TLS for the same pool.
- 240×135×2 (16bpp RGB565) = **64800 B**. At 8bpp it is **32400 B** — a 32 KB
  DRAM win that persists for the whole session.

## What was verified in the library (M5GFX / LovyanGFX v1)

- `color_depth_t` includes both `rgb332_1Byte` (8bpp direct, no palette) and
  `palette_8bit = 8 | has_palette` (256-color indexed). Palette API exists:
  `createPalette()`, `createPalette(colors,count)`, `setPaletteColor()`.
- `setColorDepth(8)` → RGB332 (`hasPalette = 8 < 8 = false`).
- `deleteSprite()` does **not** reset the depth, so one `setColorDepth` call
  after construction covers every later `createSprite` (incl. all 4 restore
  stages in `adversary_ui_restore_canvas`).

## Theme audit (why a palette *would* be exact)

- 61 distinct `rgb565()` colors across **all 6 themes + status colors**; only
  ~19 active at once (11 theme + 8 status). Fits a 256-entry palette trivially.
- **Zero hardcoded color literals** in UI draw calls — everything flows through
  `theme::` accessors.
- No anti-aliased fonts, no gradients, no alpha blends — every on-screen pixel
  is one solid theme color. Ideal for an exact palette.

## The catch that picked RGB332 first

Palette sprites interpret a draw call's color arg as a **raw index**, not RGB
(there's `getPaletteIndex()` for exact match, but the fast path doesn't
auto-map). The clean design would be to make `theme::` accessors return palette
indices. **But** the splash screen and `main.cpp:612` draw theme colors
**directly to `M5.Display`** (the true 16bpp panel), where an index would
render as near-black. So a palette needs one of:
  - route splash + that fillScreen through the canvas, or
  - dual accessors (`theme::X()` RGB for display, `theme::X_IDX()` for canvas).

Both are real work. RGB332 sidesteps it entirely: RGB565 draws auto-quantize on
the canvas, direct-to-display stays true 16bpp, `theme::` is untouched.

## This commit (RGB332 first cut)

One line in `main.cpp`: `globalCanvas->setColorDepth(8);` before the first
`createSprite`. Nothing else changes.

**Verify on-device tomorrow:**
1. Boot log free heap should be ~32 KB higher than before at canvas creation.
2. Eyeball a busy screen (Scanner, Captures, carousel) per theme. Expected
   RGB332 artifacts: neutral grays gain a slight olive tint (blue has only 4
   levels); dark blues/purples darken toward black (Cyberpunk hit hardest);
   red/green/orange themes (Matrix, T-800, Fallout, M5Stick) ~unchanged.
3. Confirm a post-Auto-Hunt sync now succeeds (largest-block headroom in the
   `[Captures] heap` logs should be much higher; the purge may not even be
   needed anymore).

## Escalation path if RGB332 colors look bad

Move to `palette_8bit` for exact colors:
1. Assign each of the ~19 active colors a fixed index; `ThemeManager` builds the
   palette (`setPaletteColor`) on load, on theme-switch, and on canvas-restore.
2. `theme::` accessors return indices for canvas draws.
3. Fix the direct-to-display draws: simplest is to route the splash screen and
   `main.cpp:612` through the canvas (then they honor the palette), OR add
   RGB-returning variants for those few call sites.
4. Re-check: no code compares theme colors by value or does color arithmetic.
