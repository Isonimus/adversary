---
id: '0168'
title: "Dashboard shell embedded in the firmware image, served from flash only"
type: architecture
status: accepted
date: 2026-11-26
supersedes: []
superseded_by: []
---

## Context

The dashboard that the Server mode serves is four static files (`index.html`,
`dashboard.js`, `index.css`, `login.html`, ~40 KB total, living in `dashboard_dev/`).
Today they are served off the SD card from `/adversary/dashboard/`
(`server_manager.cpp`, `handleRoot()` + the static-file branch in `onNotFound`). A stock
M5Burner / web-flasher install leaves that directory absent, so Server mode renders blank
until the operator either runs `scripts/deploy_dashboard.sh` (needs a repo clone) or
unzips the release asset (the v0.11.0 interim fix). Worse, a half-written or 0-byte SD
copy renders a blank-or-broken dashboard that looks identical to a working one. This is
the deploy gap recorded in LEDGER.md (line 85) and the `dashboard-deploy-gap` note.

The original plan (LEDGER line 85, roadmap row line 53) was to embed the shell **and let
an SD copy override it** ("SD overlay wins") so operators keep hot-patchability. This ADR
reverses that precedence decision, with the justification recorded below (global CLAUDE.md
§2: a reversed decision is recorded as a decision, never a silent exception). ADR-0166's
Consequences section carries a forward-looking "SD overlay wins when present" aside for the
same work; this ADR corrects it.

Key reframing that drives the decision: the shell and the data are separate. The
**data** (handshakes, credentials, packets, logs, wardriving) comes from the `/api/*`
endpoints, which read the SD live at request time and are untouched here. Embedding the
**shell** therefore does not remove the SD dependency for data; it removes the deploy step
and the corruption surface for the static files only.

Embedding a binary blob into the image is established practice in this codebase. The
Mozilla root-CA bundle is already referenced through the ESP-IDF `_binary_*_start/end`
linker-symbol idiom (`tls_upload.cpp:21-43`), and every menu icon lives in `.rodata` as a
`const uint8_t[]` in `generated_icons.h`. `board_build.embed_files` produces the same
`_binary_*_start/end` symbols for our own files, so this is the same mechanism applied to
the dashboard, driven by the build system instead of a hand-authored header.

## Decision

Embed the four `dashboard_dev/` shell files into the firmware image via
`board_build.embed_files` in the shared `[env_embedded]` block, so all three device
envs (`cardputer`, `cardputer_release`, `m5stick`) carry them; `native` is excluded (no
server). `dashboard_dev/` remains the single source the operator edits; the build embeds
it.

**Serve the shell from flash only. Drop the SD-shell serving path entirely. There is no
runtime precedence, no SD overlay, no fallback branch.** The shell ships inside the
firmware, versioned with it, matching the `/api/*` contract the same firmware defines.

Why embedded-only rather than SD-overlay-wins:

- **SD-override reintroduces the exact bug the embed exists to kill.** A corrupt or
  0-byte SD copy would shadow a good embedded shell, rendering blank with a healthy shell
  sitting unused in flash. That is a silent fallback masking a broken state, against the
  fail-loud bar (global §3).
- **Version skew.** The shell is coupled to the firmware's `/api/*` shape. A hand-dropped
  SD shell against newer firmware breaks silently. The one benefit of hot-patching is a
  liability for an artifact coupled to the same image's API.
- **The cost of giving it up is a 37-second reflash on the rare dashboard edit.** Our
  dashboard iteration rate is low, so a dev-only SD path is not worth a second serving
  code path to maintain.

**Serving rewrite (a security simplification).** The SD path builds
`"/adversary/dashboard" + path` and needs an explicit path-traversal guard
(`server_manager.cpp:302-310`). Embedded-only replaces the filesystem join with a **fixed
table of known assets** (request path -> `{start, end, contentType}`). An unknown path
404s. The traversal attack surface on the dashboard static route is **eliminated**, not
merely guarded, because there is no filesystem path to escape. The resolve step (path ->
asset descriptor or not-found, plus content-type) is extracted as a pure function over an
injected table so it is native-testable without the HTTP server or the flash symbols.

**SERVER carousel gate simplifies.** `serverEnabled` drops from "SD ready AND
`fileExists('/adversary/dashboard/index.html')`" to "SD ready" (`menu_factory.cpp:177-182`).
The "Dashboard index missing" greyed reason becomes impossible and is removed; the
per-build `fileExists` SD I/O in the carousel build drops. SERVER stays gated on an SD
being mounted, because the dashboard's purpose is viewing captured data and that data
lives on SD. "No SD card" is the sole remaining greyed reason.

**Raw (uncompressed) embed for v1.** ~40 KB against ~480 KB of app-partition headroom
(cardputer is at 76.3% today; raw pushes it to ~78%). Gzip would reclaim ~28 KB but adds a
build-time gzip step (an `extra_scripts` hook) and `Content-Encoding: gzip` handling; not
justified at current headroom. **Gzip is recorded here as the first lever to pull if flash
tightens** (notably when the Cinder dashboard rewrite lands, LEDGER line 188-203), so it is
a known move rather than a rediscovery.

The `/api/*` endpoints are unchanged: they still read SD data live.

## Alternatives considered

**SD-overlay-wins (the original plan, LEDGER line 85).** Rejected: reintroduces the
corrupt-SD-shadows-good-shell foot-gun the embed exists to eliminate, violates fail-loud,
and creates silent shell-vs-API version skew. Hot-patchability is its only benefit and is a
liability for an API-coupled shell, bought at the price of a 37 s reflash we rarely pay.

**Embedded primary with SD fallback (embedded wins, SD only if embedded absent).**
Rejected: the embedded shell is never absent by construction, so the SD branch is dead
code. Pure complexity with no reachable benefit.

**Build-flag split (release embedded, dev builds serve from SD).** Rejected: dashboard
iteration is infrequent, so a dev-only runtime divergence is not worth maintaining a
second serving path and a second code path's test burden. The 37 s reflash covers the rare
dev loop.

**Gzip + inflate-on-request for v1.** Rejected now: ~480 KB headroom makes the ~28 KB
saving unnecessary, and over a local AP a 40 KB shell loads instantly. Kept as the first
documented lever for when flash tightens.

**Status quo: keep serving from SD, ship the shell in the release ZIP / deploy script.**
Rejected: that *is* the deploy gap. It leaves the stock-install blank-dashboard failure and
the 0-byte-corruption surface in place.

## Consequences

- `platformio.ini` gains `board_build.embed_files` in `[env_embedded]`.
  `server_manager.cpp` loses the SD-shell serving path and its traversal guard and gains
  the embedded-asset table + pure resolver. `menu_factory.cpp` SERVER gate simplifies to
  SD-mounted only. Flash grows ~40 KB (cardputer 76.3% -> ~78%).
- **Zero incremental RAM/heap for the shell**: embedded files are flash-mapped `.rodata`,
  read directly. The SD file-read buffer for the shell is dropped, so the Server path's
  heap pressure is marginally *lower*, not higher. The only shell-serving RAM is the TCP
  send buffer, unchanged from today.
- **Path-traversal surface on the dashboard static route is eliminated** (fixed asset
  table, no filesystem join).
- **Deploy gap closed**: a stock M5Burner / web-flasher install renders a real dashboard
  with no SD copy and no deploy step. `scripts/deploy_dashboard.sh` and the dashboard ZIP
  release asset become redundant *for Server mode*; their fate (keep for offline browsing
  vs retire) is an implementation detail decided in the slice, not here.
- **Corrects ADR-0166's "SD overlay wins when present" aside.** With embedded-only the
  dashboard can no longer be blank-from-missing-files, so the Storage view's "Dashboard"
  row shifts from blankness-diagnosis to plain informational (it shows whether the SD also
  holds a copy, which is zero by default). ADR-0166's Storage view is otherwise unaffected.
- **`dashboard_dev/` becomes the embed source**, so the planned Cinder dashboard rewrite
  (LEDGER line 188-203) inherits embedding for free: the embed mechanism is
  content-agnostic, and Cinder's +30-50 KB still fits the ~480 KB headroom. This resolves
  the open question flagged at LEDGER line 193 (embed need not wait for the rewrite; gzip is
  the lever if the combined size ever bites).
- **SD is still required for Server (data).** The deploy gap for *data* is unchanged and
  out of scope: this ADR is about the static shell only.
- No new third-party deps, no new events, no event-bus changes.

## Verification

- **Build**: `pio run -e cardputer` and `pio run -e m5stick` serially, both green; the CI
  per-target RAM/Flash report (slice-0039) shows the ~40 KB flash bump and no RAM delta.
- **Native unit test** over the pure asset resolver against an injected table: known path
  resolves to the right descriptor and content-type; unknown path returns not-found;
  traversal probes (`/../config/settings.json`, `..%2f`, `/dashboard/../login.html`)
  resolve to not-found with no filesystem access.
- **On-device**: fresh flash with an SD that has **no** `/adversary/dashboard/` directory
  -> Server mode renders the full dashboard from flash; `/api/*` still serves captures off
  SD; traversal probes against the running Server return 404; the SERVER tile greys only
  when the SD is unmounted and the "Dashboard index missing" state no longer occurs.
