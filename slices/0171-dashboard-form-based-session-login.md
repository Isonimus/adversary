---
id: '0171'
title: "Replace dashboard Basic auth with a form-based session login"
type: slice
status: accepted
date: 2026-11-26
supersedes: []
superseded_by: []
---

## Goal

Implement ADR-0170: replace the dashboard's HTTP Basic auth with a form-based session
login. The branded `login.html` (already embedded in flash by slice-0169) becomes a real
credential form; `checkAuth()` validates a session cookie instead of firing a Basic 401
popup. The settings fields are unchanged, so there is no operator migration: they simply
see a styled login page instead of the browser's native popup.

## Definition of Done

- **Given** dashboard auth is enabled and a password is set,
- **When** the operator loads `/` in a browser with no valid session,
- **Then** they are redirected (302) to the styled `/login.html` form and see no HTTP
  Basic popup.

- **Given** the login form,
- **When** the operator submits the correct username and password,
- **Then** the server sets an `HttpOnly; SameSite=Strict` session cookie and the operator
  lands on the dashboard, and subsequent `/api/*` requests succeed with that cookie.

- **Given** the login form,
- **When** the operator submits wrong credentials,
- **Then** the form shows an inline error, no session cookie is set, and `/` still
  redirects to the login form.

- **Given** an authenticated session,
- **When** the operator triggers Logout,
- **Then** the session is cleared server-side, the cookie is expired, and `/` redirects
  to the login form again.

- **Given** a protected `/api/*` request,
- **When** it arrives with a missing or expired session cookie,
- **Then** the server returns `401` with a JSON body (not a redirect), so `dashboard.js`
  can detect expiry and route to the login form.

## Design

Per ADR-0170.

New files:

- `src/modules/server/session_store.{h,cpp}` — the pure, native-testable seam.
  - `struct Session { char token[33]; uint32_t createdMs; uint32_t lastSeenMs; bool active; };`
  - `bool extractSessionToken(const char* cookieHeader, char* out, size_t outCap)` — parse
    `adv_session=<token>` out of a `Cookie:` header that may hold several `; `-separated
    pairs; false if absent/empty.
  - `bool createSession(Session* table, size_t n, const char* token, uint32_t nowMs)` —
    fill a free slot or LRU-evict the oldest `lastSeenMs`.
  - `bool validateSession(Session* table, size_t n, const char* token, uint32_t nowMs,
    uint32_t idleTtlMs, uint32_t absTtlMs)` — true iff an active slot matches and is within
    both TTLs; touches `lastSeenMs` on success; deactivates on expiry.
  - `void clearSession(Session* table, size_t n, const char* token)` — logout.
  - All logic is pure over the injected table, `nowMs`, and token; the device supplies
    `esp_random()` and `millis()`.
- `test/test_session_store/test_session_store.cpp` — native unit tests (see Verification).

Edits to `src/modules/server/server_manager.{h,cpp}`:

- Add a `Session sessions_[kMaxSessions]` member (`kMaxSessions = 4`) and TTL constants
  (`kIdleTtlMs`, `kAbsTtlMs`) as named constants (no magic values).
- `setupWebServer()`: call `server_->collectHeaders("Cookie")` (required, or arduino-esp32
  drops the header), and register `server_->on("/api/login", HTTP_POST, ...)` and
  `server_->on("/api/logout", HTTP_POST, ...)`.
- Rewrite `checkAuth()`: if `!dashboardAuthEnabled` return true; else read the `Cookie`
  header, `extractSessionToken`, `validateSession`. On success return true. On failure,
  branch on the request URI: `/api/*` -> `send(401, "application/json", ...)`; otherwise
  `sendHeader("Location", "/login.html")` + `send(302, ...)`. Return false. The ~10
  existing `if (!checkAuth()) return;` call sites are untouched.
- `handleLogin()`: read username + password from the POST body, compare with a
  constant-time helper against the settings fields; on match, generate a 32-hex token from
  `esp_random()`, `createSession`, `Set-Cookie: adv_session=<token>; HttpOnly;
  SameSite=Strict; Path=/; Max-Age=<absTtl>`, and `send(200, "application/json",
  "{\"ok\":true}")`; on mismatch, a short fixed `delay()` then `send(401, ...)` with no
  cookie.
- `handleLogout()`: extract the token, `clearSession`, `Set-Cookie: adv_session=; Max-Age=0`,
  `send(200, ...)`.
- **Public-surface exemption (correctness-critical)**: `GET /login.html`, `POST /api/login`,
  and `POST /api/logout` must NOT go through `checkAuth()`, or the redirect-to-login loops.
  In `handleNotFound`, serve the `/login.html` asset **without** `checkAuth()` (the other
  three assets stay gated). `login.html` inlines its CSS so `index.css` need not be public.
  The existing no-password-set setup page in `handleRoot` stays public as today.
- Remove the `requestAuthentication(BASIC_AUTH, ...)` call and the `server_->authenticate()`
  use; `BASIC_AUTH` no longer appears.

Edits to `dashboard_dev/` (re-embedded automatically by `board_build.embed_files`):

- `login.html` — a self-contained styled form (username, password, submit, inline CSS)
  that `fetch`-POSTs to `/api/login`; on 200 `window.location = "/"`, on 401 show an inline
  error. No external asset references. **The login page must match the dashboard's visual
  style** (same palette, typography, and chrome) so the two read as one product: the inline
  CSS mirrors the dashboard shell's look rather than inventing a separate one. The dashboard
  is not yet theme-aware (the Tron-theme-on-dashboard gap is a separate LEDGER item); until
  it is, the login page matches the dashboard's current fixed palette, and it inherits the
  theme automatically if/when that item lands (same tokens).
- `dashboard.js` — add a Logout control (POST `/api/logout` then go to `/login.html`), and
  treat a `401` from any `/api/*` fetch as session expiry: redirect to `/login.html`.

Edit to `src/modules/storage/settings_manager.h`:

- Correct the `dashboardAuthEnabled` comment from "Enable HTTP basic auth" to describe the
  login gate. Field name unchanged (compatibility).

**What existing code was considered and reused**: `checkAuth()` stays the single gate, so
session auth reaches every route by changing one function (no per-handler edits);
`esp_random()` is the established CSPRNG (used for MACs and the AP password);
`sendHeader()` already sets response headers, so `Set-Cookie` needs no new mechanism; the
embedded-asset serving from slice-0169 is reused unchanged (only `login.html`'s content
changes). The settings fields are reused as-is.

**Deliberately not reused**: `server_->authenticate()` / `requestAuthentication(BASIC_AUTH)`
(the whole point is to drop Basic), and any SD/NVS persistence for sessions (in-RAM by
ADR-0170).

**No `any`-equivalent / typing**: concrete throughout (`char[33]`, `uint32_t`, `size_t`).
Password compare is constant-time to avoid a timing side channel.

## Verification

- **Unit tests** (`test/test_session_store/`, under `pio test -e native`):
  `extractSessionToken` for a lone cookie, a multi-cookie header, an absent token, an empty
  value, and a malformed header; `createSession` fills a slot and LRU-evicts when full;
  `validateSession` accepts a fresh token, touches `lastSeenMs`, rejects after idle TTL,
  rejects after absolute TTL, and rejects an unknown token; `clearSession` invalidates a
  token so a subsequent `validateSession` fails.
- **Build**: `pio run -e cardputer` and `-e m5stick` serially green; the CI RAM/Flash report
  reflects the small login.html/dashboard.js growth.
- **Device (operator-run)**: auth enabled, load `/` -> redirected to the styled login form,
  no Basic popup; correct creds -> cookie set, dashboard loads, captures list via `/api/*`;
  wrong creds -> inline error, no cookie; logout -> back to login; a direct `/api/info` with
  no cookie -> 401 JSON; the no-password-set setup page still appears when the password is
  empty.

## As built

Shipped as designed. The dashboard's HTTP Basic-auth gate is gone; `checkAuth()`
now reads a session cookie, validates it against an in-RAM table, and either
302s to the styled login form (for pages) or returns a JSON 401 (for `/api/*`).

- **`src/modules/server/session_store.{h,cpp}`** — pure, transport-free seam
  with `Session{token[33], createdMs, lastSeenMs, active}`,
  `extractSessionToken`, `createSession` (first-free or LRU-evict),
  `validateSession` (idle + absolute TTL, deactivates on expiry so a replay
  cannot revive it), and `clearSession`. Native-linkable: added to the native
  `build_src_filter`.
- **`server_manager.{h,cpp}`** — new members `Session sessions_[kMaxSessions=4]`
  and TTL constants `kIdleTtlMs = 30 min`, `kAbsTtlMs = 12 h`. `setupWebServer`
  calls `collectHeaders("Cookie")` (required or arduino-esp32 drops it) and
  registers `POST /api/login` + `POST /api/logout`. `checkAuth()` rewritten:
  extract the cookie, `validateSession`, on failure branch 401-JSON for `/api/*`
  vs 302 to `/login.html` for pages. `handleLogin` parses form or JSON body,
  constant-time compares against the settings fields, generates a 128-bit
  `esp_random` token (32 hex chars), `createSession`, and sets
  `adv_session=<token>; HttpOnly; SameSite=Strict; Path=/; Max-Age=<absTtl>`.
  `handleLogout` clears the slot and expires the cookie.
  `requestAuthentication(BASIC_AUTH, ...)` and `server_->authenticate()` are
  removed; `BASIC_AUTH` no longer appears.
- **Public-surface exemption** — in `handleNotFound`, the `/login.html` asset
  resolves via the embedded-shell table but bypasses `checkAuth()` (the other
  three shell files stay gated). `POST /api/login` and `POST /api/logout`
  register through dedicated routes and never call `checkAuth()`. The
  no-password-set setup page in `handleRoot` stays public as before.
- **`dashboard_dev/login.html`** — redesigned as a self-contained styled form
  (username, password, inline error, submit), inline CSS mirroring the
  dashboard shell palette (`--bg-color`, `--card-bg`, `--accent`, etc.) so the
  two read as one product. POSTs form-encoded to `/api/login`; on 200
  `window.location = "/"`, on 401 shows an inline error. No external asset
  references.
- **`dashboard_dev/dashboard.js`** — `fetchData` treats a 401 from any `/api/*`
  as session expiry and routes to `/login.html`; the direct `fetch()` in
  `showWardrivingDetail` has the same 401 branch. A new nav entry
  `data-action="logout"` POSTs `/api/logout` then redirects to `/login.html`.
- **`dashboard_dev/index.html`** — Logout nav row added at the bottom of the
  sidebar.
- **`settings_manager.h`** — `dashboardAuthEnabled` comment corrected from
  "Enable HTTP basic auth" to "Enable the dashboard login gate" (field name
  unchanged for compatibility).

The five Definition-of-Done scenarios hold by construction and build/test
proof: no valid cookie on `/` -> 302 to `/login.html` (no Basic popup); POST
`/api/login` with correct creds -> `Set-Cookie: adv_session=...; HttpOnly;
SameSite=Strict`, 200, dashboard loads; wrong creds -> inline error, no cookie
(covered by `test_session_store` invariants plus the server's
`constantTimeEquals`); logout -> `clearSession` + expired cookie, next `/`
redirects; a protected `/api/*` without a cookie -> 401 JSON (covered by the
`checkAuth` branch).

Verification run in-session: `pio test -e native` all green (21 new cases in
`test_session_store`); `pio run -e cardputer` SUCCESS; `pio run -e m5stick`
SUCCESS. On-device sign-in against the real AP is the remaining operator-run
verification step.
