---
id: '0033'
title: "In-field sessions and deliverable reports: raw device log, interpreted dashboard report"
type: architecture
status: accepted
date: 2026-09-17
supersedes: []
superseded_by: []
---

# ADR-0033 — In-field sessions and deliverable reports: raw device log, interpreted dashboard report

## Context

The device produces evidence — handshakes, credentials, scan results, band sweeps,
detections, attack outcomes — but no *deliverable*. Everything lands as pcaps and manifest
entries on the SD card. For a red-team tool the report **is** the product: the value of an
engagement is the defensible, readable artifact that states what was tested, what was
found, what it means, and offers proof — not the raw pile of captures. The gap between that
pile and a client-ready artifact is unclosed, and that gap is where the professional value
sits.

Three existing decisions and one invariant constrain how this may be built:

- **No PSRAM (invariant).** The deployment Cardputer ADV and the StampS3 original have no
  PSRAM chip; the ~205 KB internal SRAM is the entire budget (see `slices/0008`, CLAUDE.md).
  Compiling a rich, cross-referenced report on-device — rewriting a growing JSON document in
  RAM — fights this budget for no gain. A field device also loses power mid-operation
  routinely, so any format that must be rewritten as it grows risks a half-written,
  unrecoverable document exactly when the engagement mattered.
- **The manifest is the sole metadata store.** Storage Phase 4 (memory:
  `storage-rework-decision`) retired per-capture JSON sidecars and made the BSSID-keyed
  manifest the single home for capture metadata. A session that *copied* capture data into
  itself would reintroduce precisely the duplicated-metadata problem that rework deleted.
- **The EventBus already carries the events a recorder needs.** slice-0028 dissolved screen
  action downcasts by routing them through the EventBus, and the bus already emits
  `HANDSHAKE_CAPTURED`, `CREDENTIAL_CAPTURED`, `WIFI_SCAN_COMPLETED`, and
  `ATTACK_STATE_CHANGED` from their modules. A recorder can therefore be a pure *subscriber*
  — no module needs to know sessions exist.
- **Server-mode dashboard exists.** The dashboard is served from SD in server mode and is
  where rendering and printing already live; it has a real rendering engine and no 205 KB
  ceiling.

The design tension is where interpretation happens and what the device is responsible for.
Putting finding-severity logic and report compilation on the device would burn the scarce
resource (SRAM) to produce a rigid artifact that is expensive to iterate. Putting them on
the dashboard keeps the device's job small and robust and lets the report format evolve
without a firmware flash.

## Decision

1. **A session is an opt-in, in-field recording window; free-running is the default mode.**
   Capture always works without a session and always writes to the manifest as today. A
   session must be **manually created and activated** ("hit record"); there is no implicit
   or auto-started session. One session is active at a time — never two. This is chosen for
   predictability over magic: an auto-started session would have to invent an engagement
   name, scope, and authorization it cannot know, and a wrong authorization record is worse
   than none. The accepted cost is that an operator **can forget to start recording**; the
   mitigation is that free-running still persists everything to the manifest, so nothing is
   lost — it simply is not attributed to a report until a session is running.

2. **The firmware emits raw events only; the dashboard interprets.** The device records
   *what happened* (an observation, a capture, a credential, a sweep) with no severity, no
   finding classification, and no remediation text. All interpretation — mapping
   observations to findings, assigning severity, coverage/negative-result analysis,
   remediation guidance, redaction — lives on the dashboard, where it is cheap to iterate
   and costs no SRAM. The device never ships a value judgement it would have to re-flash to
   change.

3. **On-device session state is an append-only NDJSON event log plus a small metadata
   header.** Each recorded EventBus event is one line appended to `events.ndjson`
   (one JSON object per line, carrying a monotonic sequence number, timestamp, and GPS fix
   when available). Session identity, operator, engagement, and authorization/scope live in
   a separate small `meta.json` written at creation. Append-only is crash-safe by
   construction: power loss truncates at most the final line, never corrupts the whole
   document, and nothing in RAM grows with session length.

4. **A session references the manifest; it never copies capture data.** Events in the log
   point at captures by BSSID / manifest key. The manifest stays the single source of
   capture metadata (honours Storage Phase 4). The session log is a *temporal and
   contextual overlay* — a time window, operator context, and references — not a second copy.

5. **The recorder is a single EventBus subscriber; no feature module is aware of sessions.**
   Recording is one `SessionRecorder` that subscribes to the report-worthy event types and
   appends them when a session is active. Modules keep emitting their events unconditionally
   (honours slice-0028's decoupling); the recorder is the only component that knows a session
   exists. Consequently, **any report-worthy outcome that is not published to the bus is
   invisible to the report** — closing that emission gap is a precondition of implementation,
   not a later cleanup (see Consequences).

6. **Authorization and scope are first-class session metadata.** `meta.json` records the
   engagement name, operator, date, and — critically — the authorized scope (target
   SSIDs/BSSIDs, geographic bounds, time window). This is what makes the deliverable
   defensible and lets the dashboard distinguish in-scope findings from incidentally
   observed collateral. It is part of the session from creation, not an afterthought.

7. **Report compilation is dashboard-only for now.** The device writes the raw log; the
   dashboard reads it, compiles the interpreted `report.json`, renders HTML, and prints to
   PDF. There is deliberately **no** on-device report-compilation path in this decision. An
   offline on-device compile (for hand-off without a dashboard) is a possible later
   extension and is deferred to the ledger, not built now.

8. **The report schema is a versioned contract between firmware and dashboard.** Both the
   NDJSON event line shape and the compiled `report.json` carry a `schemaVersion` from the
   first release. The two are decoupled: the dashboard may evolve the interpreted report
   format freely as long as it can still read the versioned raw log, without a firmware
   change.

## Consequences

- **The emission surface must be complete before the recorder is built.** Because the
  recorder only sees what reaches the bus (decision 5), a feature that produces a
  finding/capture but does not publish it — or publishes only a coarse state-change and
  drops the payload — yields a report with a silent hole that no one notices until a real
  engagement. Implementation is therefore gated on a full available-vs-missing event
  inventory across all of `src/modules/`, with the missing events added first. This gating
  work is tracked in the ledger; no part of the session recorder is built yet.
- **The device stays small and robust; the report stays free to evolve.** SRAM is spent
  only on appending lines, never on holding or compiling a document, so the no-PSRAM budget
  is respected. Report richness — new finding types, severity rubric changes, new remediation
  copy — is a dashboard change, shippable without touching firmware.
- **A forgotten session loses attribution, not data.** Operators who never hit record still
  get full manifest persistence; those captures simply are not bound to a report until a
  session is started. This is the accepted cost of decision 1 and is not a defect.
- **One session at a time keeps the model KISS.** No merge semantics, no concurrent-session
  arbitration, no per-event session routing. Multi-session or nested engagements, if ever
  wanted, are a new decision superseding this one.
- **The append-only log is the system of record; the compiled report is derived.** A report
  can always be recompiled from the raw log with a newer interpretation, which means a
  dashboard bug in interpretation is fixable retroactively against past engagements. The raw
  log must therefore be preserved alongside the compiled artifact, not discarded on compile.
