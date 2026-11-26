---
id: '0169'
title: "Embed the dashboard shell in firmware, serve from flash only"
type: slice
status: accepted
date: 2026-11-26
supersedes: []
superseded_by: []
---

## Goal

Implement ADR-0168: embed the four dashboard shell files (`index.html`, `dashboard.js`,
`index.css`, `login.html`) into the firmware image via `board_build.embed_files` and serve
them from flash only, dropping the SD-shell serving path entirely. The `/api/*` data
endpoints are untouched (they still read SD live).

Closes the LEDGER.md deploy-gap item (line 85, formerly "bake the dashboard into the
firmware"): a stock M5Burner / web-flasher install currently leaves `/adversary/dashboard/`
absent and Server mode renders blank until the operator clones the repo for
`scripts/deploy_dashboard.sh` or unzips the release asset. After this slice the shell ships
inside the firmware, versioned with it, and a half-written or 0-byte SD copy can no longer
shadow it (the SD-override precedence was reversed in ADR-0168). This slice deletes that
ledger line as it closes the item.

## Definition of Done

- **Given** a Cardputer freshly flashed with this firmware and an SD card that has **no**
  `/adversary/dashboard/` directory,
- **When** the operator opens SERVER, joins the device AP, and loads the dashboard in a
  browser,
- **Then** the full dashboard shell renders (HTML, CSS, JS all load from flash) and the
  `/api/*` endpoints still serve captures from the SD.

- **Given** the Server is running,
- **When** a request arrives for a path outside the known asset set, including traversal
  probes (`/../config/settings.json`, `/dashboard/../login.html`, `..%2fsettings.json`),
- **Then** the Server responds 404 and performs no SD filesystem access for that request.

- **Given** no SD card is mounted,
- **When** the operator views the carousel,
- **Then** the SERVER tile is greyed with reason "No SD card", and the former "Dashboard
  index missing" greyed reason never appears (the shell is always present in flash).

## Design

Per ADR-0168.

New files:

- `src/modules/server/dashboard_assets.{h,cpp}` — the native-testable seam.
  - `dashboard_assets.h` declares
    `struct DashboardAsset { const char* requestPath; const uint8_t* start; const uint8_t* end; const char* contentType; };`,
    a pure `const DashboardAsset* resolveDashboardAsset(const DashboardAsset* table, size_t count, const char* requestPath)`
    (returns `nullptr` for an unknown path = 404), and an inline
    `size_t dashboardAssetLen(const DashboardAsset&)` = `end - start`.
  - `dashboard_assets.cpp` holds **only** the pure resolver (exact-match of `requestPath`
    against the table, with `"/"` normalized to `"/index.html"`). It references no
    `_binary_*` symbols, so it compiles and links under `native`. Because resolution is
    exact-match against a fixed known set, a traversal path simply fails to match and 404s:
    there is no filesystem path to escape, which is the attack-surface elimination ADR-0168
    calls for. The old explicit traversal guard (`server_manager.cpp:302-310`) is therefore
    deleted, not ported.
- `test/test_dashboard_assets/test_dashboard_assets.cpp` — native unit tests (see
  Verification).

Edits:

- `platformio.ini` — add to `[env_embedded]`:
  `board_build.embed_files = dashboard_dev/index.html, dashboard_dev/dashboard.js, dashboard_dev/index.css, dashboard_dev/login.html`
  (relative paths; no absolute paths per global §3). These emit
  `_binary_dashboard_dev_<file>_start/_end` symbols (exact spelling confirmed at build time
  from the map file). `native` does not inherit this (it does not extend `env_embedded` for
  the server).
- `src/modules/server/server_manager.cpp`:
  - Declare the four asset byte-spans via the established
    `extern const uint8_t ... asm("_binary_...start")` idiom already used for the CA bundle
    (`tls_upload.cpp:21-43`), and build the `DashboardAsset` table (path -> span -> content
    type), replacing the inline `contentType` if/else ladder at lines 317-323.
  - `handleRoot()`: serve the `index.html` asset from flash. The password-setup intercept
    (line 251-256) and `checkAuth()` gate stay unchanged.
  - `handleNotFound()`: resolve the request path through `resolveDashboardAsset`; on a hit,
    `checkAuth()` then send the flash bytes; on a miss, fall through to the existing `/api/*`
    routing and the final 404. The SD `fileExists` + `SD.open` + `streamFile` branch
    (lines 312-335) is removed.
  - Login path: serve the embedded `login.html` asset instead of reading it from SD
    (lines 362-365).
  - Emit the flash bytes with `WebServer::send_P(code, contentType, (PGM_P)start, len)` using
    the explicit `dashboardAssetLen` length. `send_P` with an explicit length is required
    because `embed_files` bytes are **not** null-terminated (a `String`/`send` overload that
    assumes a C string would overrun). This is the one serving-mechanics gotcha from ADR-0168.
- `src/ui/menu_factory.cpp` (lines 177-182) — simplify the SERVER gate from
  "SD ready AND `fileExists('/adversary/dashboard/index.html')`" to "SD ready". Drop the
  `fileExists` call and the two-reason string; the sole greyed reason becomes "No SD card".
- `LEDGER.md` — delete the embed-dashboard item (line 85) as this slice closes it.
- **Boyscout deletion**: `src/utils/path_security.h` and `test/test_server_security/`
  are removed. `isSafeWebPath` existed solely to guard the SD path-join this slice
  deletes, and `server_manager.cpp` was its only production caller; with exact-match
  resolution there is no path to escape, so both the helper and its test become dead
  code (global §3: dead code is deleted). Traversal coverage moves to
  `test_dashboard_assets` at the layer that now matters.

**What existing code was considered and reused**: the `_binary_*_start/end` extern idiom is
reused verbatim from `tls_upload.cpp` (the CA-bundle embed), so no new mechanism is
introduced. `checkAuth()` and the password-setup intercept are reused unchanged. The inline
content-type mapping is folded into the asset table rather than kept as a parallel if-ladder.
`generated_icons.h` is the in-repo precedent that `const`/embedded data lives in flash
`.rodata` at zero RAM cost.

**Deliberately not reused**: `SD.open` / `streamFile` for the shell (the whole point is to
stop serving the shell from SD) and the `"/adversary/dashboard" + path` join (replaced by
exact-match resolution, which removes the traversal surface).

**No `any`-equivalent / typing**: all concrete (`const uint8_t*`, `size_t`, `const char*`).

**Flash/RAM**: ~40 KB flash added (cardputer 76.3% -> ~78%), zero incremental RAM/heap (the
shell is flash-mapped `.rodata`; the SD shell-read buffer is dropped). Raw embed this slice;
gzip is the recorded future lever (ADR-0168), not implemented here.

## Verification

- **Unit tests** (`test/test_dashboard_assets/test_dashboard_assets.cpp`, run under
  `pio test -e native`) over `resolveDashboardAsset` against a stub table (dummy non-null
  `start`/`end`): `"/"` resolves to the `index.html` asset; `"/dashboard.js"` resolves with
  `application/javascript`; `"/index.css"` -> `text/css`; an unknown path (`"/nope.txt"`)
  returns `nullptr`; traversal probes (`"/../config/settings.json"`,
  `"/dashboard/../login.html"`, `"..%2fsettings.json"`) all return `nullptr`;
  `dashboardAssetLen` returns `end - start`.
- **Build**: `pio run -e cardputer` and `pio run -e m5stick` serially (CLAUDE.md
  convention), both green. The CI per-target RAM/Flash report (slice-0039) shows the ~40 KB
  flash increase and no RAM delta.
- **Device verification (operator-run)**:
  1. Flash this firmware; on an SD with no `/adversary/dashboard/` directory, open SERVER,
     join the AP, load the dashboard: the full shell renders from flash and `/api/files/*`
     still lists SD captures.
  2. With the Server running, request `/../config/settings.json` and `/nope.txt`: both 404,
     no SD file is opened.
  3. Boot without an SD: the SERVER tile is greyed with "No SD card"; the "Dashboard index
     missing" reason never appears.

## As built

Shipped as designed. The four shell files are embedded via
`board_build.embed_files` in `[env_embedded]` and served from flash only; the
SD-shell serving path is gone.

- **`src/modules/server/dashboard_assets.{h,cpp}`** — `DashboardAsset{requestPath,
  start, end, contentType}`, `dashboardAssetLen`, and the pure
  `resolveDashboardAsset` (exact-match, `"/"` -> `/index.html`, nullptr on miss).
  The `.cpp` binds no flash symbols, so it compiles natively (added to the native
  `build_src_filter`).
- **`server_manager.cpp`** — declares the four byte spans via the
  `asm("_binary_dashboard_dev_<file>_start/_end")` idiom (same as the CA bundle),
  builds the asset table, and sends bytes with `send_P` + explicit length (embedded
  bytes are not null-terminated). `handleRoot` serves the embedded index,
  `handleNotFound` resolves the shell through the table, and the login branch serves
  the embedded `login.html`. `checkAuth()` and the password-setup intercept are
  unchanged. The `/api/*` data endpoints are untouched.
- **`menu_factory.cpp`** — SERVER gate simplified to `sdManager.isReady()`; the
  "Dashboard index missing" reason is removed, leaving "No SD card".
- **Symbol names**: PlatformIO's `embed_files` produced
  `_binary_dashboard_dev_<file>_start/_end` exactly as guessed; both device builds
  linked with no undefined references, so no map-file correction was needed.
- **Boyscout**: `src/utils/path_security.h` and `test/test_server_security/` deleted
  (the exact-match resolver removed their only caller; traversal coverage now lives
  in `test_dashboard_assets`).

The three Definition-of-Done scenarios hold by construction and build/test proof:
the shell is always present in flash (scenario 1, with `/api/*` still SD-backed);
an unknown or traversal path returns nullptr from the resolver and falls through to
the 404 path with no SD access (scenario 2, covered by `test_dashboard_assets`); and
the SERVER tile greys only on "No SD card" (scenario 3). On-device confirmation of
scenarios 1-2 against a running AP is the remaining operator-run step.

Verification run in-session: `pio test -e native` 1181/1181 green (including the new
`test_dashboard_assets`, 9 cases); `pio run -e cardputer` SUCCESS (flash 76.3% ->
77.4%, ~+40 KB, no RAM delta); `pio run -e m5stick` SUCCESS; docs linter 0/0.
