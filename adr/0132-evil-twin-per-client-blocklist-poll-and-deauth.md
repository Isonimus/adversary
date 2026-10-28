---
id: '0132'
title: "Evil-twin per-client ActionMenu: poll-and-deauth blocklist, not an event-driven AID kick"
type: architecture
status: accepted
date: 2026-10-28
supersedes: []
superseded_by: []
---

## Context

The evil-twin CLIENTS view lists the stations connected to our rogue softAP
(`esp_wifi_ap_get_sta_list`, MAC + RSSI) as a selectable `ScrollableList`, but ENTER was a
no-op (ledgered since slice-0076). The v0.7.0 "Scanner interaction" thread turns these
read-only lists into act-on-item lists through the shared `ui::ActionMenu`, the same modal
the WiFi/BLE scanners already open on a selected item (ADR-0128).

The clients here are attached to *our own open* AP, which shapes what a per-client action
can honestly do. One useful action is a one-shot deauth: it bounces the client, which
re-triggers the captive portal (a second chance at credentials from a device that connected
but never submitted). The other is a persistent block: evict a nuisance or a defender's
scanner and keep it out for the session. The two actions are the agreed scope (chosen
2026-10-28).

Eviction from our own AP has two candidate mechanisms:

1. **Event-driven AID kick.** Register a `WIFI_EVENT_AP_STACONNECTED` handler; when a
   blocklisted MAC (re)associates, read its association id from the event payload and call
   `esp_wifi_deauth_sta(aid)`. Instant, aid-accurate.
2. **Poll-and-deauth.** In the existing `EvilTwin::update()` loop, compare the live station
   list against the blocklist each cycle and inject a targeted deauth frame at any
   blocklisted MAC through the already-built `DeauthAttack::sendTargetedBurst` path.

Two measured facts decide between them. First, the codebase has **zero** precedent for WiFi
event handlers: a grep for `WiFi.onEvent` / `esp_event_handler_register` across `src/`
returns nothing, so option 1 introduces that infrastructure for the first time. Second, that
handler runs in the WiFi task, not the main loop, so its read of the blocklist races the
UI's writes from the input path, which under the project's concurrency rules (CLAUDE.md:
"Mutex Protection for shared resources") forces a mutex around an 8-entry set. Option 2 adds
neither: `update()` is already the single-threaded home of the periodic real-AP deauth, and
`sendTargetedBurst` is already native-tested.

## Decision

ENTER on a client in the CLIENTS view opens the shared `ui::ActionMenu`, titled with the
client MAC, offering `D` "Deauth once" and `B`/`U` "Block device"/"Unblock device" (the
label and shortcut follow the selected client's current blocked state). This matches the
scanner action-modal idiom exactly.

Per-client eviction is **poll-and-deauth inside `EvilTwin::update()`, not event-driven**. A
fixed-capacity MAC blocklist (sized to `EVIL_TWIN_MAX_CLIENTS`) is checked against the live
station list each update cycle; any blocklisted MAC still associated is deauthed with a
targeted burst whose frame source is the **live softAP BSSID** read from
`esp_wifi_get_mac(WIFI_IF_AP, ...)`, which is correct whether or not the BSSID was cloned
(the cloned case overwrites the AP MAC, so reading the live value covers both). "Deauth
once" uses the same `sendTargetedBurst` path for a single burst without touching the
blocklist.

The blocklist's add / remove / contains / toggle logic is pure and host-tested in
`test_rogue_ap`; the `esp_wifi`/`sendTargetedBurst` calls are device-only (`#ifndef
UNIT_TEST`), the pure-core / device-shell split this codebase already uses. The blocklist is
session-scoped: it is cleared on `stop()` (and therefore on each new attack), never
persisted.

## Consequences

- **Eviction is near-instant, not instant.** A blocklisted device can briefly reconnect
  between poll cycles (sub-second on a ≤8-client AP) before the next `update()` deauths it
  again. This is deliberate and must be stated as such: it is deauth-on-sight, not a MAC
  filter. The ESP32 Arduino softAP exposes no association deny-list, so a true refused
  association is not available to us regardless.
- **"Deauth once" is a nudge, not a kick.** The client reconnects to our open AP within
  seconds; its value is re-triggering the captive portal, and the UI copy must not oversell
  it as removal.
- **No new concurrency or event-handler surface.** The attack stays in the single-threaded
  update model, so there is no mutex and no `WiFi.onEvent` lifecycle to manage on
  start/stop.
- **Hardware-verify is deferred** to the ledger (device radio behaviour, bus-factor-1,
  ADR-0022): confirm on-device that a blocked client is evicted and kept out across
  reconnects, that unblocking lets it back, and that a one-shot deauth re-shows the portal.
- If a future need wants instant aid-accurate kicks (for example a high-churn AP where the
  reconnect window matters), that is the trigger to revisit option 1 and pay for the event
  handler plus the blocklist mutex then, not now.
