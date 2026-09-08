---
id: '0016'
title: "Browser-based web flasher page (ESP Web Tools on GitHub Pages)"
type: slice
status: accepted
date: 2026-09-08
supersedes: []
superseded_by: []
---

## Goal

Collapse first-flash onboarding from "install Python + PlatformIO, clone, resolve the
toolchain, `pio run -t upload`" (~30 min) to "open a page, click Install" (~30 s). A
static page hosting [ESP Web Tools](https://esphome.github.io/esp-web-tools/) flashes the
firmware straight from the browser over **Web Serial** — no local toolchain.

This is the consumer half of slice-0014. That slice already builds the merged **factory
image** (`bootloader + partitions + boot_app0 + app @ 0x0`, QIO/80m/8MB, validated by
`esptool merge-bin`) and publishes it plus `SHA256SUMS.txt` as assets on a `v*`-tagged
GitHub Release. The factory image is exactly what a web flasher writes to `0x0`. So this
slice adds no build/packaging logic — only a manifest, a page, and the wiring to keep both
zero-maintenance across releases.

## Scope

**Cardputer (ESP32-S3) only** — the same and only target slice-0014 ships a factory image
for. The M5StickC Plus2 is on `huge_app.csv` (single 3 MB app, no OTA) and has no factory
asset; the page states it is "not web-flashable yet" rather than offering a broken button.
When a real ADV target or an M5Stick factory image lands, it becomes another `chipFamily`
entry in the manifest.

## Definition of Done

- **Given** a supported browser (desktop Chrome/Edge/Opera) on the hosted HTTPS page with a
  Cardputer connected over USB
- **When** the operator clicks *Connect & Flash*
- **Then** ESP Web Tools reads `manifest.json`, fetches the factory image from the latest
  release, and flashes it to `0x0` — installing the firmware with no local toolchain

- **Given** a firmware release is cut (a new `v*` tag)
- **When** the page is loaded afterwards, unchanged
- **Then** it serves the **newest** firmware automatically — the manifest points at
  `releases/latest/download/…`, so no page or manifest edit is needed per release

- **Given** an unsupported browser (Firefox/Safari/mobile) or a non-HTTPS origin
- **When** the page loads
- **Then** the install control is replaced by an explanatory slot and the operator is
  routed to the manual `esptool` fallback — no dead button

## Design

### Static page + manifest, on GitHub Pages

Two files under `web/`: `index.html` (self-contained — inline CSS, no build step) and
`manifest.json`. Deployed to GitHub Pages by `.github/workflows/pages.yml` using only
first-party `actions/*` (`configure-pages` → `upload-pages-artifact` → `deploy-pages`), so
this adds **no** third-party action to the supply chain — consistent with the slice-0011 /
slice-0014 posture. The workflow runs on pushes to `main` that touch `web/**` (the page is
static; it does not need redeploying per firmware release) plus `workflow_dispatch`.

**One-time manual step:** repo Settings → Pages → Source = *GitHub Actions*. This is a
human, credential-bound repo setting, not something CI can self-enable.

### The manifest points at `latest/download` — the page never changes

`manifest.json` references the factory image by a **fixed** URL:

```
https://github.com/Isonimus/adversary/releases/latest/download/adversary-cardputer-factory.bin
```

GitHub's `/releases/latest/download/<name>` redirect makes the page and manifest static
forever: every new release auto-serves the newest firmware with zero page maintenance. The
binary is cross-origin (the redirect lands on `objects.githubusercontent.com`), but that
host returns `access-control-allow-origin: *`, so ESP Web Tools' `fetch` succeeds — the
same pattern ESPHome and Tasmota web installers use.

Two constraints this imposes, both handled:

1. **A stable asset name must exist on the release.** slice-0014 publishes only
   *version-stamped* assets (`…-<tag>-factory.bin`), which `/latest/download/` cannot
   resolve. `release.yml` now also publishes an unversioned copy
   `adversary-cardputer-factory.bin` (a two-line `cp` + one extra asset arg). The stable
   copy is byte-identical to the version-stamped one and is covered by `SHA256SUMS.txt`.
2. **`/latest/` excludes prereleases.** The redirect resolves only the release marked
   *Latest*, so releases must **not** be cut with `--prerelease` (the current `gh release
   create` does not). A comment in `release.yml` records this coupling.

### Manifest install semantics

`chipFamily: "ESP32-S3"`, one part at `offset: 0` (the factory image already carries
bootloader + partitions + app). `new_install_prompt_erase: true` offers a full-flash erase
on install — appropriate for a factory image that may overwrite unrelated firmware; it
wipes NVS (saved Wi-Fi / settings) but **not** the SD card (captures are safe). The page
copy states this explicitly. `version` is `"latest"` rather than a pinned string, matching
the `latest/download` source — the page links to the releases list for the exact version.

### One runtime dependency, pinned

The page loads the `<esp-web-install-button>` custom element from
`unpkg.com/esp-web-tools@10.4.0` (verified latest at authoring). This is a **page** runtime
dependency, not part of the firmware build; pinned exactly, bump deliberately.

### Visual identity

The page reuses the on-device `red-team` theme verbatim (black ground, `#ff2020` accent,
`#2a2a2a` hairlines — the palette of `dashboard_dev/index.css`) so it reads as the same
product. Typography deliberately diverges from the dashboard's glass-sans into a
monospace instrument-panel register (system font stack, no webfonts → self-contained and
reproducible). No gradient, no centered card, no webfont — a distinct authored look rather
than a template.

### Why no native unit test

Like slices 0010 and 0014, this slice is static assets + CI configuration — there is no
application logic to exercise. Correctness is: valid JSON manifest, well-formed
Actions/Pages workflow, a verified dependency pin, and ultimately a real Pages deploy
flashing a real device.

## Verification

- **Local (the ceiling here):** `web/manifest.json` is valid JSON with the ESP Web Tools
  schema shape (`builds[].chipFamily`, `parts[].path`/`offset`); `web/index.html` renders
  the full design offline (only the live flash needs HTTPS + Web Serial + a device); the
  `esp-web-tools` pin `10.4.0` was confirmed against the npm registry as the current latest;
  `pages.yml` is a well-formed Pages deploy using first-party actions with least-privilege
  `pages: write` + `id-token: write`; the `release.yml` change adds a stable-named,
  hash-covered factory asset without altering the validated merge command.
- **Remote / real run (deliberate future acts):** (1) enable Pages (Source = GitHub
  Actions) and confirm `pages.yml` deploys `web/`; (2) on the live HTTPS page in desktop
  Chrome, flash a real Cardputer and confirm it boots the firmware; (3) confirm the next
  `v*` release publishes `adversary-cardputer-factory.bin` and that `/latest/download/`
  resolves it. Tracked as a LEDGER follow-up until observed.

### Amendment — 2026-09-08: dogfood a one-shot wordmark reveal

The page dogfoods our own MIT library `@isonimus/glitch-js` (v2.0.0) — but narrowly, on
purpose. Of its effect set (rgb-split, scanlines, hologram, scramble, …) only
`Effects.decrypt` is used, and only once: a ~1.3 s forward decrypt reveal of the wordmark
on load, then static. The atmospheric effects are deliberately left unused — the page's
restraint is the design; page-wide glitch texture would tip it into template kitsch, the
exact thing the visual language avoids.

Three properties held:

- **Progressive enhancement.** The real text `Adversary` is in the HTML; the effect only
  animates an inner `.wm-text` span. If the module fails to load or JS is off, the wordmark
  renders static and legible — the effect is never a legibility dependency.
- **`prefers-reduced-motion`** gates it off, matching the cursor blink.
- **Self-contained.** The library is **vendored** into `web/lib/glitch.es.js` (unmodified,
  under a provenance banner) rather than CDN-loaded, so the page keeps its "one unavoidable
  external runtime dep" property — esp-web-tools is the flash engine and must be external; a
  cosmetic effect lib is avoidable-external, so it is vendored. `glitch-js` is a *page*
  asset, not a firmware build dependency, so it is not in `DEPENDENCIES.lock.md`.

API confirmed against the vendored file: `new Glitch(el, { effects:[Effects.decrypt(...)],
trigger:'always' })` auto-starts; the decrypt self-completes; the frame loop is stopped
after the reveal settles. (The README's `trigger:'manual'` does not exist in v2.0.0.)

### Amendment — 2026-09-08: CORS forced a same-origin binary (design correction)

The "manifest points at `releases/latest/download/…`, page never changes" design above
was **wrong**, and it failed on the first real device flash with "Failed to fetch" (after
the chip handshake succeeded). GitHub release assets send **no `access-control-allow-origin`
header** — verified across the whole redirect chain (`github.com` → 302 →
`release-assets.githubusercontent.com` → 200, no CORS header with an `Origin` request) — so
the browser blocks a cross-origin `fetch()` of the firmware from the Pages origin. (The
claim that ESPHome/Tasmota point web installers at GitHub release assets was a
misremembering; they host firmware same-origin.)

Fix: **serve the firmware same-origin with the page.** `web/manifest.json` now uses a
relative path (`./adversary-cardputer-factory.bin`), and `pages.yml` pulls the latest
release's factory image into the site at deploy time (`gh release download`, not committed
to git — `*.bin` is git-ignored). Consequences:

- `pages.yml` gains a `release: [published]` trigger — a new firmware release must refresh
  the binary the page serves, not just a `web/**` push. The stable-named release asset
  (from `release.yml`) is still published and is what the deploy step downloads by name.
- The page is no longer strictly "static across releases" — the served binary is
  release-versioned, refreshed on each publish. The HTML/CSS is still static.
- The `releases/latest/download/…` URL remains valid for CLI/M5Burner users; only the
  in-browser flasher needed same-origin.

## As built

_To be completed once Pages is enabled and a real browser flash is observed green;
tracked in the LEDGER until then._
