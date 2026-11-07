---
id: '0147'
title: "Decouple NTP sync from the WIFI_CONNECTED handler"
type: slice
status: accepted
date: 2026-11-07
supersedes: []
superseded_by: []
---

## Goal

Stop the `WIFI_CONNECTED` event handler from blocking up to 10 s while it waits for NTP.
`TimeManager`'s handler called `syncFromNTP()` synchronously, and `EventBus::publish()` runs
handlers inline, so the main loop (which drains the bus via `WiFiConnection::update()`) stalled
right as WiFi came up and uploads started, observed as a UI pause during sync. The clock
already survives a slow or failed sync through the SD-cache and GPS tiers, so the sync does not
need to be inline. Closes the deferred LEDGER item.

## Definition of Done

- **Given** WiFi has just connected and SNTP will take several seconds (or never completes)
- **When** the `WIFI_CONNECTED` handler runs
- **Then** it starts the sync and returns immediately (no `delay()` poll loop on the dispatch
  thread), and the main loop keeps rendering while the sync is in flight.

- **Given** an async sync started by the handler
- **When** the SNTP client reports completed on a later main-loop tick
- **Then** `update()` latches the NTP time (source becomes NTP, the time cache is saved), and if
  the deadline passes first it abandons the attempt without changing the active source.

- **Given** a caller that must have the clock set before it proceeds (the TLS upload time gate,
  the manual "Sync Now")
- **When** it calls `syncFromNTP()`
- **Then** that call still blocks until the sync completes or times out and returns the outcome,
  exactly as before.

## Design

Everything that writes `TimeManager` state (`syncFromGPS` from `GPSManager::update()`, the NTP
latch) and everything that touches the SD time cache already runs on the main loop thread: the
only `xTaskCreate` sites are GPS *detection* and BLE, neither of which writes time or SD. A
background sync task would be the first code to write time state and the SD cache off the main
loop, and there is no SD mutex, so it would break that single-threaded invariant. The fix keeps
the sync on the main loop and makes it non-blocking instead, which is possible because SNTP is
already asynchronous: `configTime()` only starts the client, and `sntp_get_sync_status()` reports
when it is done.

- `startSntp_()` calls `configTime()` (returns at once) and arms `ntpPending_` + a start
  timestamp. `beginNtpSync()` is the fire-and-forget entry the `WIFI_CONNECTED` handler now uses.
- `update()` is a new main-loop tick: a no-op unless a sync is pending, otherwise one
  non-blocking `sntp_get_sync_status()` poll. It is wired into `loop()` next to the other manager
  updates.
- `syncFromNTP()` stays blocking for the two callers that check its result immediately
  (`tls_upload::timeSyncedForTls()`, settings "Sync Now").
- The latch-vs-timeout decision is a pure free function, `evaluateNtpPoll(syncComplete,
  elapsedMs, timeoutMs)`, shared by both the polled `update()` and the blocking loop (DRY). A
  completed sync latches even past the deadline, since a late success still beats the SD-cache
  tier. The function is pure so it is unit-tested off-device without mocking the ESP-only SNTP
  client.

Out of scope: a true RTC, and changing the GPS/SD tiering. The blocking `syncFromNTP()` is kept
deliberately, not removed.

## Verification

- `pio test -e native -f test_time_manager`: the four `evaluateNtpPoll` cases (pending / latch /
  timeout / completion-wins-over-timeout), proven fail-before (treating a completed sync as
  pending fails the two latch cases) / pass-after; the existing time tests stay green.
- `pio test -e native`: full suite green.
- `pio run -e cardputer` and `pio run -e m5stick`: both build.
- On-device: on WiFi connect the UI no longer pauses; the serial log shows "Starting async NTP
  sync" immediately and a later "NTP Sync SUCCESS" once the client reports completed.

## As built

`TimeManager` gained `beginNtpSync()` (async start), `update()` (non-blocking main-loop latch),
and the private `startSntp_()` / `latchNtpSuccess_()` helpers; the `WIFI_CONNECTED` handler calls
`beginNtpSync()`; `syncFromNTP()` is unchanged in contract and now composes the same helpers plus
the shared `evaluateNtpPoll()` decision. `loop()` ticks `TimeManager::update()`. Regression in
`test/test_time_manager` (`evaluateNtpPoll`). Fail-before confirmed, pass-after 11/11; full native
suite and both device builds green.
