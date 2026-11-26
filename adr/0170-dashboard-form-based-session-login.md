---
id: '0170'
title: "Form-based session login for the dashboard, replacing HTTP Basic auth"
type: architecture
status: accepted
date: 2026-11-26
supersedes: []
superseded_by: []
---

## Context

The dashboard authenticates with HTTP Basic auth: `checkAuth()` calls
`server_->requestAuthentication(BASIC_AUTH, "Adversary Dashboard")`
(`server_manager.cpp`), the browser renders its native credential popup, and the
`Authorization: Basic` header is replayed on every request. slice-0169 embedded a
`login.html`, but that file is only a captive-portal landing page (its one control
is an `<a href="/">Sign In</a>` link), never a credential form; the real credential
entry has always been the Basic popup.

Three problems with Basic here:

- **Unbranded and unstylable.** The popup shows a raw realm string, cannot carry the
  product's look, and is an awkward sheet on mobile captive-portal flows. `handleRoot`
  already carries a workaround note that Android Chrome aborts a bare 401 with
  `ERR_HTTP_RESPONSE_CODE_FAILURE`, so the project has already felt this edge.
- **Maximal password exposure.** Basic re-sends the password on *every* request, so
  its exposure window on the (WPA2-encrypted but shared) AP link is the whole session.
- **Operator expectation.** The branded `login.html` is expected to collect
  credentials; today it cannot.

`checkAuth()` is the single gate: it is called from ~10 route handlers, each as
`if (!checkAuth()) return;`. That centralization is the lever this ADR uses.

## Decision

Replace HTTP Basic auth with a **form-based session login**, entirely server-side in
`ServerManager`. The settings fields (`dashboardAuthEnabled`, `dashboardUsername`,
`dashboardPassword`) are unchanged; only the mechanism changes, so there is no
migration.

**Flow.**

- `login.html` becomes a **self-contained** credential form (username + password,
  inline CSS, no external asset dependency) that POSTs to `/api/login`.
- `POST /api/login` verifies username + password (constant-time compare) against the
  settings fields. On success it mints a random session token, stores it in a small
  fixed in-RAM session table, and sets the session cookie; on failure it returns 401
  after a short fixed delay.
- `checkAuth()` is rewritten to validate the **session cookie** instead of Basic: read
  the `Cookie` header, extract the token, look it up in the session table with idle +
  absolute expiry. On a miss it answers by request type, inspecting the URI itself so
  no call site changes: **302 to `/login.html`** for page requests, **401 JSON** for
  `/api/*` (so `dashboard.js` can detect expiry and redirect client-side). All ~10
  `if (!checkAuth()) return;` sites stay exactly as they are.
- `POST /api/logout` clears the session and expires the cookie; `dashboard.js` gains a
  logout control.

**The public (no-auth) surface is exactly three things**: `GET /login.html`,
`POST /api/login`, and the existing no-password-set setup page in `handleRoot`.
Everything else stays gated. This resolves the chicken-and-egg that a naive version
would hit (if reaching `/login.html` required auth, `checkAuth`'s redirect to it would
loop): `login.html` inlines its own styles so `index.css` need **not** be public, and
`/api/login` + `/login.html` are explicitly exempted from `checkAuth`.

**Cookie.** `Set-Cookie: adv_session=<token>; HttpOnly; SameSite=Strict; Path=/;
Max-Age=<ttl>`. HttpOnly keeps JS (hence XSS) from reading it; SameSite=Strict covers
CSRF for a same-origin, single-operator surface. **No `Secure` flag**, because the
dashboard is plain HTTP over the SoftAP: confidentiality still rests on the WPA2 AP key
(slice-0036), exactly as Basic's credentials did, now with an opaque, revocable,
single-use-at-login token instead of the reusable password.

**Session model.** A fixed array of `N = 4` sessions in RAM:
`{char token[33]; uint32_t createdMs; uint32_t lastSeenMs; bool active;}`, ~200 bytes
total. Idle TTL (sliding on `lastSeenMs`, e.g. 30 min) plus absolute TTL (on
`createdMs`, e.g. 12 h); LRU eviction when full. Sessions are lost on reboot, which is
acceptable (re-login is cheap; nothing requires persistence). The token is 32 hex chars
(128 bits) from `esp_random()`.

**Native-testable seam.** `src/modules/server/session_store.{h,cpp}` holds the pure
logic: cookie-token extraction, session create / validate / touch / clear, idle +
absolute expiry, and eviction, all over an injected table, an injected `nowMs`, and an
injected token string. The device supplies `esp_random()` and `millis()`; the policy is
host-tested without either. This mirrors the `storage_overview` / `dashboard_assets`
pure-seam pattern.

## Alternatives considered

**Keep HTTP Basic auth.** Rejected: unbranded, unstylable, replays the password every
request, and the poor captive-sheet UX has already needed a workaround in `handleRoot`.

**Persist sessions to SD or NVS.** Rejected: adds I/O and flash wear for a property
nobody needs (a session surviving reboot). In-RAM is simpler and loses nothing the
operator cares about.

**JWT / signed stateless cookie.** Rejected: needs a device signing key and an HMAC per
request, for no gain over a random token looked up in a 4-slot table. The table is
simpler and supports instant server-side revocation (logout), which a stateless token
cannot without a denylist that reintroduces the state it was meant to avoid.

**Add a separate CSRF token on top of the cookie.** Rejected for this threat model:
SameSite=Strict plus a same-origin API covers CSRF for a single-operator local AP. A
CSRF token is extra moving parts with no matching threat here; recorded as a lever if a
cross-origin surface is ever added.

**Heavyweight rate limiter / account lockout store.** Rejected as over-built: an
attacker must already be on the WPA2 AP, so a fixed per-failure delay plus a short
cooldown after repeated failures raises the cost enough without a stateful limiter.

## Consequences

- `checkAuth()` changes **once**; every protected route inherits session auth with no
  per-site edit. `login.html` and `dashboard.js` are updated; two endpoints are added
  (`/api/login`, `/api/logout`); `session_store.{h,cpp}` + its native test are new. No
  new third-party dependencies.
- **Security posture improves**: the password crosses the wire once (at login) instead
  of on every request; the session token is opaque, HttpOnly, SameSite=Strict, and
  server-revocable. The missing `Secure` flag is an honest limitation of the HTTP-over-
  SoftAP transport, surfaced in docs and Settings copy; it is no weaker than Basic was.
- The **at-rest plaintext password** (LEDGER line 109) is unchanged and still out of
  scope: this slice is transport/mechanism, not credential storage.
- The `Cookie` request header must be registered via `WebServer::collectHeaders()` at
  setup, or arduino-esp32's `WebServer` will not capture it. This is the one easy-to-miss
  wiring step.
- The settings-field comment "Enable HTTP basic auth" is corrected to describe the login
  gate; the field name stays for compatibility.
- Pairs with slice-0169: `login.html` is already embedded in flash, so this slice only
  changes its *content* (to the form) and the auth mechanism, not how it is served.

## Verification

- **Native unit tests** over `session_store`: cookie-token extraction (present / absent
  / multiple cookies / malformed / token with no value); create then validate; idle
  expiry; absolute expiry; `lastSeenMs` touch on validate; eviction when the table is
  full; clear/logout invalidates a token.
- **On-device**: with auth enabled, loading `/` redirects to the styled `/login.html`
  with no Basic popup; correct credentials set the cookie and land on the dashboard;
  wrong credentials show an inline error (and no cookie); a protected `/api/*` with a
  missing or expired cookie returns 401; logout returns to the login form; the
  no-password-set setup intercept and the AP-password path still work.
- **Build**: `pio run -e cardputer` and `-e m5stick` serially green; `pio test -e native`
  green.
