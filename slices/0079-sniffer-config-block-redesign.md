---
id: '0079'
title: "Sniffer config block redesign"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

The sniffer screen's capture controls are hidden hotkeys (`+`/`-` channel step, `h` hop
toggle, `m` capture-mode cycle, `s` save-to-file) with no on-screen affordance, so the
operator could not discover the channel control during slice-0077 testing. This slice
surfaces them as a selectable config block at the top of the STATS view, mirroring the
Deauth attack screen's config-then-live-content layout: Channel / Hopping / Mode / Save to
SD as rows that cycle on Enter, a terminal Start/Stop Capture action row at the bottom of
the config block (the deauth "Start Attack" idiom), and the live packet counters kept
below a separator. It also fixes, at its root, the footer `[stop]`-on-a-fresh-screen
desync found while testing slice-0077 (a freshly recreated, non-capturing `SnifferScreen`
still showed the `[stop]` action because the label did not derive from real capture
state): start/stop is now driven by the new action row and the status-bar `REC` indicator
shows only while actually capturing. Two capture-integrity readouts are restored/added at the
same time: the Save to SD row carries the live pcap size (and a write-error marker), which the
pre-redesign stats column had and the counter grid had dropped; and the `REC` tag carries the
dropped-frame count under flood (slice-0078's backpressure telemetry, previously tracked but
never shown). The shared config-row / action-button component stays deferred to the later
global config-screen refactor (the ledger's config-form unification and shared action-button
items), per rule-of-three: this is the second deauth-style instance of the idiom, not the
third, and the extraction must also reconcile the distinct `< value >`-editing config-form
family (BadBLE/Karma) before it is right.

## Definition of Done

- **Given** capture is running with hopping off
- **When** the operator selects the Channel row and presses Enter
- **Then** the channel value cycles 1..13 with wraparound, shown inline on the row, and the
  sniffer retunes to the displayed channel

- **Given** the STATS view with capture running
- **When** the operator selects the Hopping row and presses Enter
- **Then** hopping flips on/off (value shown inline), and while it is on the Channel row shows
  "auto" and is itself disabled (the sniffer auto-cycles, so a manual channel has no effect)

- **Given** the STATS view (capturing or not)
- **When** the operator selects the Mode or Save to SD row and presses Enter
- **Then** that row shows its current value inline and toggling it changes the capture-mode
  filter / pcap-write setting respectively

- **Given** the sniffer is not capturing (idle), or was entered with a fixed target network
- **When** the operator navigates the config rows
- **Then** the Channel and Hopping rows are shown disabled (greyed) and selection skips over
  them so they cannot be cycled, while Mode / Save / Start remain active; they enable only once
  capture is running and no target is set (Channel additionally only while hopping is off)

- **Given** a freshly shown, never-started sniffer screen
- **When** nothing has been started
- **Then** the terminal action row reads "Start Capture" (not "Stop"), the status-bar tag
  shows no `REC`, and the footer carries no capture indicator at all
- **And When** the operator selects the action row and presses Enter
- **Then** capture starts, the row reads "Stop Capture", and a steady red `REC` tag appears
  in the status bar; pressing Enter again stops capture, the row returns to "Start Capture",
  and the red `REC` tag disappears

- **Given** capture is running in the STATS view
- **When** packets arrive
- **Then** the two-row, three-column counter grid below the separator — Packets / Beacons /
  Probes on top, Data / EAPOL / Deauth below — updates while the config block stays in place
- **And When** the operator presses `l`
- **Then** the full-height PACKET_LOG view shows, and the status-bar red `REC` tag reflects
  the real capture state (present while capturing, absent otherwise)

- **Given** capture is running with Save to SD on
- **When** packets are written to the pcap file
- **Then** the Save to SD row's value shows the running file size (e.g. `on 12.3K`), and shows
  `on ERR` if a write fails

- **Given** capture is running under heavy traffic that overruns the handoff queue
- **When** frames are dropped (`m_droppedFrames > 0`)
- **Then** the status-bar tag shows the dropped-frame count (`REC !N`) in both views, so the
  lossy capture is visible rather than silently incomplete

## Design

**Approach.** The STATS view gains a top config block rendered as a selectable list through
the existing `ScrollableList` + `ListScrollModel`, with a per-row draw lambda (label +
inline `< value >`), exactly as `deauth_screen.cpp`'s `drawConfig` does
(`deauth_screen.cpp:248-293`). Rows, top to bottom: Channel, Hopping, Mode, Save to SD,
then a terminal "Start Capture" / "Stop Capture" action row that mirrors deauth's "Start
Attack" last row. A separator line (`theme::ACCENT_DARK()`, as deauth draws at
`deauth_screen.cpp:245`) divides the config block from the live counters, which keep the
current compact two-column layout from `drawStats` (`sniffer_screen.cpp:1048-1121`). The
PACKET_LOG view is unchanged.

**Enter dispatch reuses existing actions.** `activateConfigRow` switches on the selected
config-row index and runs the action for that control: `stepChannel(true)`, `toggleHopping()`,
`cycleCaptureMode()` (the `m` logic), the `m_saveToFile` toggle (the `s` logic), and
`toggleCapture()` for the action row. `stepChannel`/`toggleHopping` are small shared helpers
so the config rows and the `+`/`-`/`h` accelerators run one implementation and cannot drift;
both helpers are no-ops unless capturing and not targeting (`esp_wifi_set_channel` only works
with WiFi up), and `stepChannel` is additionally inert while hopping. The raw `+`/`-`/`h`/`m`/
`s` hotkeys are kept as accelerators so existing muscle memory does not regress; they are
simply no longer the *only* affordance.

**Capture-state display and footer-bug fix.** The buggy indicator at
`sniffer_screen.cpp:1023-1026` is the footer right-content, which renders only `[REC]` or
`[STOP]` keyed strictly off `m_capturing`, with no idle/start label — so a fresh screen
unconditionally reads `[STOP]`. Rather than patch that label, this slice **removes the
footer capture indicator entirely** (a redundant second place to show state) and shows
capture state once, in two complementary spots: (a) the terminal action-row label, derived
from `m_capturing` ("Start Capture" when false, "Stop Capture" when true), which gives the
start action a real affordance in the STATS view; and (b) a status-bar tag shown only while
`m_capturing`. Deleting the footer indicator fixes the desync at its root — the stale label
no longer exists. The REC tag also carries the dropped-frame count ("REC !N") when
`m_droppedFrames > 0`: slice-0078 added that backpressure telemetry (frames dropped when the
handoff queue fills under flood) but never surfaced it, so a lossy capture looked healthy;
now the operator sees the loss in both views.

**Capture file status on the Save row.** The pre-redesign stats column showed the running
pcap size ("File: 12.3KB") and a write-error marker; the counter grid dropped both, so an
operator could no longer confirm the capture was actually landing on SD. Rather than spend a
counter cell, the Save row's value carries it: `on 12.3K` while writing, `on ERR` on a write
failure, plain `on`/`off` otherwise. The row already exists, so this restores the lost
readout without new layout.

**The REC tag routes through the existing status-bar tag slot, not a bespoke heading.** The
top 20px is the shared `ui::StatusBar`, already rendered by `drawHeader`
(`sniffer_screen.cpp:1034-1046`) with the screen title plus a `centerContent` tag that
currently carries the capture-mode name (`StatusBar::render`, `status_bar.h:325-361`). The
hardcoded right-side badges there (WiFi/battery/SD/GPS/RFID/cap/heap) are all *global*
device state true on every screen; a capture-REC badge is *screen-specific*, so baking it
into `StatusBar` as another hardcoded badge would leak sniffer state into a shared
component. Instead: reuse the `centerContent` slot, and add one minimal, generic knob to
`StatusBar::render` — an optional `centerColor` parameter defaulting to
`theme::TEXT_SECONDARY()` (so every existing caller is unchanged) — so the sniffer can draw
that tag in `theme::ERROR()` red. The "REC" text and its red colour live in the sniffer;
`StatusBar` learns nothing about capturing. While `m_capturing`, the sniffer passes "REC"
in red to the center slot; otherwise it passes the capture-mode name in its usual colour,
as the status bar does today, so the mode stays glanceable in both views when idle. The tag
is **steady red, not blinking** — a blink would buy only a cosmetic cue at the cost of a
cadence constant and a redraw-timing coupling (the PACKET_LOG view redraws rarely under
light traffic), underused logic this screen does not need. The tag shows in both views, so
capture state is visible whether watching stats or the log.

**Disabled rows.** Channel and Hopping are live-capture controls, so they are disabled
(greyed, and skipped by the selection nav) whenever they cannot take effect: when not
capturing (WiFi is down, so `esp_wifi_set_channel` fails), and in fixed-target mode (the
target owns the channel). Channel is additionally disabled while hopping is on, since the
sniffer then auto-cycles channels and shows "auto". This "disable a control that currently
has no effect" rule follows the pattern already used elsewhere (e.g. the IR Learn row), and
is the honest fix for the first on-device finding that Channel/Hopping did nothing when
pressed from an idle screen. `ListScrollModel` has no concept of a non-selectable row (the
ledger tracks this), so this slice hand-rolls the skip inline. That inline skip is the
*second* screen-local skip-nav after settings/IR; a third concrete consumer would trigger
extracting a shared selectable-predicate into the model (per that ledger item) — noted here,
deliberately not done in this slice.

**Layout math.** Screen 240x135; the status bar takes y=0..20 and the footer is 16px
(`ui::FOOTER_HEIGHT`), so the content area runs y=20..119 = 99px. Five config rows at
`theme::LIST_ROW_HEIGHT` (14px, `theme_manager.h:420`) occupy ~70px (y=20..90) — the *same*
row pitch deauth's config list already uses, so this is no deviation (an earlier draft wrongly
assumed a 20px row). A separator sits at ~y=91, leaving ~27px (y=92..119) for the counter
grid: two rows at ~12px. The counters are a **two-row, three-column grid** holding all six
traffic counts with full type names — Packets / Beacons / Probes on the top row, Data / EAPOL
/ Deauth on the bottom — on an ~80px column pitch (x = 4 / 84 / 164). The names fit the
columns comfortably at typical counts; a very high (6-digit) count can crowd a column, checked
on-device. Nothing is dropped from the old `drawStats` counter set: Channel and File leave the
grid because Channel is now a config row and the pcap size moved onto the Save row (below). The
final row pitch and vertical fit are confirmed on-device and recorded in `## As built`.

**What existing code was considered.** Searched `src/ui/components/` for a shared
config-row / label-value / `< value >`-selector component: none exists
(`character_selector.h` is a full-screen text-entry wheel; `ScrollableList` /
`ListScrollModel` provide only generic list scaffolding — selection index, scroll offset,
selection-band and scrollbar paint). Deauth hand-rolls its whole config block with no shared
component. **Reused:** `ScrollableList` + `ListScrollModel` for the config list; the
existing `ui::StatusBar` `centerContent` tag slot for the REC indicator (the same slot that
already carries the mode name); and every existing capture action verbatim
(`cycleCaptureMode`, `toggleCapture`, the channel step, `setChannelHopping`, `m_saveToFile`).
**Minimal generic addition:** one optional `centerColor` parameter on `StatusBar::render`
(default `theme::TEXT_SECONDARY()`, so every existing caller is byte-for-byte unchanged),
which lets any screen tint its center tag — not sniffer-specific, so no screen state leaks
into the shared component. **Deliberately not reused / not extracted:** a shared config-row
or action-button component — this is the 2nd deauth-style instance (rule of three), and two
ledger items already own that extraction as a design-first refactor that must reconcile this
idiom with the separate BadBLE/Karma `< value >`-editing config-form family; extracting from
two examples would fix the wrong shape and scope-creep this slice. A capture-REC badge was
**deliberately not** added as a hardcoded `StatusBar` badge (the WiFi/SD/GPS/etc. pattern),
because those are global device state and REC is screen-specific.

## Verification

- `pio run -e cardputer` SUCCESS — the screen compiles into the device build. Exact result
  recorded in `## As built`.
- `pio test -e native` stays green with an unchanged count: `sniffer_screen.{h,cpp}` live
  under `ui/`, outside the native `build_src_filter` allowlist, so the screen carries no
  host test — the same recorded constraint as the slice-0069..0078 UI work. The
  native-compiled sniffer policy (`makeSnifferAttackTarget` over `sniffer_types.h`,
  `test_sniffer_actions`) is a presentation-free concern this slice does not touch. Count in
  `## As built`.
- `node scripts/lint-docs.mjs` 0 errors.
- On-device DoD (operator, on the Cardputer, because the rendering and interaction exist
  only on the display stack): walk each Definition-of-Done scenario — config rows render and
  cycle on Enter; the Start/Stop row toggles capture and its own label; a freshly shown
  screen reads "Start Capture" with no `REC` (footer desync gone); fixed-target mode greys
  and skips Channel/Hopping; the counters update below the separator while the config block
  holds; and `l` toggles the full-height log with a correct `REC` indicator. Because the
  `centerColor` knob touches the shared `StatusBar`, also spot-check that another screen's
  status bar (e.g. the scanner's mode tag) still renders in its original colour — the
  default param must leave it unchanged. Operator sign-off and the final row-pitch/counter
  layout recorded in `## As built`.

## As built

Shipped as designed, with the refinements below folded in during on-device review.

**What shipped.** The STATS view renders a five-row config block directly under the status bar
— Channel, Hopping, Mode, Save to SD, and a terminal Start/Stop Capture action row — through
`ScrollableList` + a per-row `label + value` lambda, then a `theme::ACCENT_DARK()` separator,
then a two-row by three-column live counter grid (Packets / Beacons / Probes · Data / EAPOL /
Deauth, full names). `;`/`.` navigate the rows and Enter activates the selected one, sharing
`stepChannel`/`toggleHopping`/`cycleCaptureMode`/the save toggle/`toggleCapture` with the
retained `+`/`-`/`h`/`m`/`s` accelerators. Capture state shows once: the Start/Stop row label
plus a steady red `REC` status-bar tag (via the new optional `StatusBar::render` `centerColor`
param); the old footer `[REC]`/`[STOP]` indicator was deleted, which fixes the `[stop]`-on-a-
fresh-screen desync at its root. The PACKET_LOG view is unchanged.

**Deviations / refinements from the first design, all operator-confirmed on-device:**
- **Default selection** lands on the Start/Stop action row so Enter still starts capture on a
  fresh screen (preserves the long-standing reflex).
- **Idle status tag** shows the capture-mode name (red `REC` only while capturing), rather than
  nothing, so the mode stays glanceable when idle.
- **Channel/Hopping are disabled unless capturing** (and not targeting; Channel additionally
  disabled while hopping, showing `auto`). This answered the first on-device finding — the rows
  did nothing when pressed from an idle screen, because `esp_wifi_set_channel` only works with
  WiFi up and `setChannelHopping` was needlessly gated on `RUNNING`. Disabling them until they
  can act (the pattern used e.g. by the IR Learn row) is the honest fix; `stepChannel` and
  `toggleHopping` were extracted so the rows and the `+`/`-`/`h` keys share one guarded body.
- **Save to SD row carries the live write status** (`on 12.3K` while writing, `on ERR` on a
  write failure), restoring the pcap-size readout the counter grid had dropped.
- **`REC` tag carries the dropped-frame count** (`REC !N`) when the handoff queue overruns under
  flood, surfacing slice-0078's previously-invisible backpressure telemetry.
- **Counter labels use full type names** (operator judged the columns had room); a 6-digit count
  can crowd a column, accepted as a cosmetic edge.
- The invert-the-block-order option (counters above config) was considered and rejected: deauth
  uses separate CONFIG/RUNNING states and places its config list at the top, so config-on-top is
  the consistent, cursor-forward choice.

**Verification.** `pio run -e cardputer` SUCCESS (Flash 74.6%, 2,494,507 bytes). `pio test -e
native` 844/844 passed, unchanged — the screen is outside the native `build_src_filter`
allowlist, so no host test, per the slice-0069..0078 precedent. `node scripts/lint-docs.mjs` 0
errors. Operator flashed and signed off on the Cardputer across the config block, the Start/Stop
row and `REC` tag, the footer desync being gone, the idle/fixed-target/hopping disabling of
Channel and Hopping, the full-name counters, and the Save-row file size. The dropped-frame
`REC !N` path is wired and shows only under genuine flood, so it was verified by code rather
than forced on-device.
