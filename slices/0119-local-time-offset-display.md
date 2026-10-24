---
id: '0119'
title: "Operator local-time offset (display-only, manual 15-min granularity)"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

Let the operator read local time on-screen instead of UTC, per ADR-0118: a signed
15-minute-granular offset applied only at display sites, with every persisted or exported
timestamp (WiGLE CSV, session ids, filenames, stored epochs) staying UTC.

## Definition of Done

- **Given** the offset is UTC+0 (default)
- **When** the operator opens the wardriving idle screen
- **Then** the clock reads the same as before (UTC), so the stock experience is unchanged.

- **Given** the offset is set to +5:30 (India)
- **When** the operator views the wardriving clock at 18:00 UTC
- **Then** it shows 23:30, and a WiGLE CSV row and session id written at that moment still carry
  the 18:00 UTC stamp.

- **Given** the offset is -3:00 and it is 01:00 UTC
- **When** the clock renders
- **Then** it shows 22:00 of the previous day (date rolls back with the time).

- **Given** the operator opens Settings > Time Management
- **When** they edit the Time Zone row
- **Then** it steps in 15-minute increments across UTC-12:00 .. +14:00 and displays as
  "UTC+5:30" / "UTC-3:00" / "UTC" at zero.

## Design

Per ADR-0118. Two pure helpers in `src/utils/time_format.h`, host-tested:

- `localTimeFields(time_t utcEpoch, int16_t offsetMinutes, struct tm* out)`: adds the offset to
  the epoch and breaks it down with `gmtime_r` (reentrant, reads no `TZ` global), so it yields
  local wall-clock fields without ever touching the canonical UTC clock. Returns false only if
  `gmtime_r` rejects the shifted epoch.
- `formatUtcOffset(int16_t offsetMinutes, char* out, size_t outLen)`: renders the settings-row
  value "UTC", "UTC+5:30", "UTC-3:00" (the minute part uses the absolute value so a negative
  offset signs only the hour).

Settings: `int16_t tzOffsetMinutes = 0` on `SystemSettings` (the general-device bucket, surfaced
under the existing Time Management section), with JSON load/save. A `SettingItem` NUMBER row
"Time Zone" over an int32_t `tempTzOffsetMinutes_` (min -720, max 840, step 15); `hide()` clamps
and commits; `formatNumberValue` special-cases the temp pointer to call `formatUtcOffset`.

Display wiring:
- `WardrivingScreen::drawTime` swaps its `gmtime_r(&now, ...)` for
  `localTimeFields(now, offset, ...)` reading `settings.system.tzOffsetMinutes` live; the
  synced-guard and source tag are unchanged.
- `SDManager::generateTimestampFilename` switches `localtime_r` to `gmtime_r` (ADR-0118 boyscout:
  a no-op today since `TZ` is unset, but makes the UTC intent explicit and immune to a future
  `TZ` set).

Untouched (stay UTC by construction): `formatWigleTimestamp`, `formatSessionId`, and all stored
epochs. The CSV writer never sees the offset.

## Verification

- `pio test -e native -f test_time_format`: new, covering localTimeFields (whole/half/quarter
  hour, negative, day/month/year rollover across the boundary) and formatUtcOffset (zero,
  positive, negative, half and quarter hour sign placement).
- `pio test -e native -f test_settings_manager`: stays green; the new field compiles into the
  JSON load/save as a one-line mirror of its sibling scalars (not separately round-trip-tested,
  matching the existing convention: the bug-prone logic lives in the pure helpers above).
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS (serial; -j 2 avoids
  the host OOM).
- On-device: a ledger verify line tracks confirming the wardriving clock shifts with the offset
  while a CSV row written at the same instant stays UTC.

## As built

(pending)
