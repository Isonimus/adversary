---
id: '0118'
title: "Local-time offset is display-only; every persisted or exported timestamp stays UTC"
type: architecture
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Context

The device is a field tool, and on a wardriving walk the operator glances at the on-screen
clock constantly. That clock renders UTC (`drawTime` in `src/ui/screens/wardriving_screen.cpp`
calls `gmtime_r`), so an operator in UTC+2 reads a time two hours off their wristwatch. Giving
them local time is a v0.6.0 "Field usability" item. The question this ADR settles is *which*
timestamps the offset may touch, because the firmware emits time in six places and they do not
all have the same audience.

Internal time is already cleanly UTC. `TimeManager::now()` returns a UTC epoch:
`syncFromGPS` takes UTC fields, and `syncFromNTP` computes the epoch while explicitly setting
`TZ=UTC0` and restoring it afterwards, precisely to keep the stored epoch free of any zone.
(The `tzOffsetH` parameter on `syncFromNTP` is dead: all three call sites pass nothing.) So the
canonical clock is UTC; the only question is formatting.

The ledger flagged one factual unknown: whether the WiGLE CSV `FirstSeen` field wants device
local time or UTC. It was worth checking because if WiGLE wanted local time, the current
`gmtime_r` emission would be a latent correctness bug that a timezone feature should fix. The
WiGLE 1.4 specification (api.wigle.net/csvFormat-1_4.html) states the field is "in SQL
seconds-precision time format (`YYYY-MM-DD hh:mm:ss`), assumes UTC". So the hypothesis was
wrong: our emission is already correct, and shifting the CSV by a local offset would *introduce*
a regression, not fix one. That reframes the whole feature from a correctness change into a
display convenience, and it is the reason this decision has to be written down: the obvious
"apply the user's timezone everywhere we format time" instinct would corrupt the export.

## Decision

**A local-time offset is a display concern only. The offset is applied solely to timestamps
rendered on-screen for the operator to read. Every timestamp that is persisted to storage or
exported in a file keeps UTC.** Concretely, drawing the dividing line through the six surfaces:

Gets the offset (operator reads it on the glass, nothing downstream parses it):
- the wardriving live clock (`WardrivingScreen::drawTime`), and
- any other on-screen rendering of wall-clock time added later (capture-list times, a status-bar
  clock, etc.).

Stays UTC (lands in a file or a record that something else reads back):
- the WiGLE CSV `FirstSeen` field, because the spec mandates UTC;
- the wardriving session id `WD-YYYYMMDD-HHMMSS`, so the id matches the UTC timestamps inside the
  CSV it names rather than drifting from them;
- SD filename stamps (`SDManager::generateTimestampFilename`), so files sort chronologically and
  unambiguously regardless of a later offset change;
- all stored epochs (capture registry, handshake records), which are raw `time_t` and must stay a
  single monotonic UTC scale so a mid-session offset change can never reorder or duplicate them.

The offset is expressed as a **signed minute count** on a canonical settings field, in 15-minute
granularity spanning UTC-12:00 to +14:00. Fifteen-minute steps are required to represent the
real zones that are not whole hours (India +5:30, Nepal +5:45, Chatham +12:45). **There is no
DST handling**: the operator adjusts the offset by an hour when their zone changes. A field tool
with a two-row settings form and no reliable network does not justify the cost of POSIX `TZ`
strings (a text editor or a multi-hundred-entry zone picker in the UI) plus the integration
hazard of a persistent `TZ` env var fighting the UTC-preservation dance in `syncFromNTP`. The
DST adjustment is a twice-a-year one-field edit; automating it is not worth that machinery at
this stage.

The conversion is a **pure function** over epoch and offset, host-tested, that produces the
broken-down local fields for rendering. `TimeManager::now()` is unchanged and keeps returning
UTC; the offset is never folded into the canonical clock, only into the formatting step at a
display site.

## Consequences

- The WiGLE export stays spec-correct by construction: the CSV writer never sees the offset, so
  no future edit to the display path can corrupt `FirstSeen`. This invariant binds later work,
  which is why it is an architecture decision and not just a slice note.
- The operator sees local time where it helps (the walk clock) and UTC where it must be
  unambiguous (files they will upload, sort, or correlate). A capture saved at 23:30 UTC and a
  CSV row at 23:30 UTC carry the same stamp, even if the operator's screen said 01:30.
- `generateTimestampFilename` currently calls `localtime_r`, which resolves to UTC only because
  no `TZ` is set; it is switched to explicit `gmtime_r` so the intent is honest and cannot change
  if some other code ever sets `TZ`. This is a no-op in behavior today.
- A mid-session offset change (operator crosses a zone, or fixes the setting) re-renders every
  on-screen clock immediately and changes no stored or exported value, so there is no
  reconciliation, migration, or re-sort to perform.
- Adding DST later, if ever justified, supersedes this ADR's no-DST clause rather than bolting on
  a silent exception.
- The pure offset-to-local-fields function is unit-tested on the host (whole-hour, half-hour,
  three-quarter-hour, negative, day/month/year rollover across the boundary); the display wiring
  and the settings row are device-only.
