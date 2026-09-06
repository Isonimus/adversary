---
id: '0005'
title: "Capture quality: pulse preview & protocol detail (IR + Sub-GHz)"
type: slice
status: accepted
date: 2026-09-06
supersedes: []
superseded_by: []
---

## Goal

Make a captured signal **legible before it is trusted**, across both capture features
shipped so far (slice-0003 CC1101 OOK, slice-0004 cap IR). Two coupled additions:

1. **A static pulse-train preview** rendered *after* a capture (on the save step) and again
   on a saved code's detail/action screen — a logic-analyzer-style on/off strip of the
   recorded edges. Its job is to let the operator *see* whether they caught a clean,
   structured, repeated frame or a short/ragged/noise fragment, and discard-and-retry
   before saving. This directly addresses the field failure that motivated the slice: an
   incomplete IR capture that replays as "the emitter blinks once but the TV ignores it"
   ([LEDGER] cap IR follow-up). The fail-loud half — rejecting `overflow`/over-length
   captures outright in `ir_capture` — already shipped ahead of this slice; the preview
   catches the *plausible-but-wrong* captures that pass structural validation.
2. **Protocol / RAW surfacing** — show an IR code's decoded protocol (NEC/Sony/RC5/…) or
   the **RAW** fallback in the UI, following the scanner's existing convention for
   multi-valued type, and mark RAW distinctly because it is the less-robust encoding and
   the likeliest to be the one that fails.

Explicitly out of scope: any rendering *during* a transmit (see the hard constraint in
Design), live/animated capture (an IR frame is over in tens of ms — nothing to animate),
protocol decode for Sub-GHz (OOK stays raw, per slice-0003), and editing a captured train.

## Definition of Done

- **Given** a capture (IR or OOK) has just completed and structurally validated
- **When** the operator reaches the name/save step
- **Then** a pulse-strip preview of the recorded edges is drawn before naming, and the
  operator can discard without saving if it looks incomplete or like noise

- **Given** a saved code in the list
- **When** the operator opens its detail/action screen
- **Then** its encoding is shown — the decoded protocol and bit count for a Parsed IR code,
  or **RAW** plus edge count (and, for OOK, the frequency) — with RAW visually distinct
  (dimmed / `TEXT_SECONDARY`) from a clean decoded protocol

- **Given** a replay/transmit is executing
- **When** the timing-critical waveform is being emitted
- **Then** the UI draws nothing that competes for the CPU during the send — the preview is
  static and painted only before and after, never mid-transmit — and the target still
  actuates (no regression to the bit-banged IR carrier or the RMT OOK train)

- **Given** an IR code that decoded to a known protocol (Parsed, so no raw timing train is
  stored)
- **When** its detail screen is shown
- **Then** the protocol summary is presented and the preview degrades gracefully (a
  schematic/summary, not a blank or crashing strip) rather than fabricating edges

## Design

### Hard constraint — never render during a send

The slice-0004 bring-up proved IR emission is CPU bit-bang whose carrier is corrupted by
any competing work (`IRsend::mark()` toggles under a busy-wait; a stray ISR or a canvas
flush stretches the pulse and garbles the frame). The OOK path is RMT-timed but its screen
shares the same core. Therefore the preview is a **static** artifact computed from stored
edges and painted **only** on the save step and the detail screen — the render path holds
no live capture and the transmit path performs no draw. This is the load-bearing design
rule of the slice, not an optimisation.

### Representation — pulse strip, not waveform

Both IR (carrier-gated mark/space) and CC1101 OOK (on-off keying) are **binary** in the
recorded domain: the stored train is a list of on/off durations. The honest rendering is a
logic-analyzer square strip (high = mark/on, low = space/off), **not** a sine "wave" — a
sine would misrepresent an OOK/PWM signal. This matches the operator's own instinct
("pulse-like … if the representation fits better") and is the accurate one.

The pure work is **downsampling the duration list to the display width**: given the edge
durations and a target pixel width, produce per-column fill so a 60–900-edge train maps
onto ~230 px without lying about structure (long gaps stay visibly long; a dense burst
stays dense). That mapping is a free function with no display dependency, so it is
native-testable; the component only blits its output.

### Data availability — the Parsed vs RAW asymmetry

- **OOK** signals are always raw (`OokSignal::durationsUs`) — a full train to draw.
- **IR RAW** codes carry `IrSignal::timingsUs` — a full train to draw.
- **IR Parsed** codes store only `{protocol, value, bits}` (slice-0004 KISS decision) — **no
  raw train**. We do **not** re-persist a redundant raw copy (SD cost, and it re-opens the
  decode-vs-raw choice slice-0004 closed). For Parsed, the detail screen shows the protocol
  summary (already computed by `describeSignal()`, today only logged to Serial) and the
  preview degrades to that summary rather than reconstructing edges. Reconstructing a Parsed
  train via `IRsend`'s internal timing is possible but is TX-oriented and not worth the
  coupling for a preview — revisit only if operators ask to see Parsed waveforms.

### UI surfacing — reuse the scanner's convention

The scanner already sets the house rule: a **multi-valued type** (WPA2/WEP/OPEN) lives in
the info/subtitle line via `getSecurityString()`, while single-char coloured **badges** are
reserved for **boolean** flags. IR protocol is multi-valued, so it belongs in the
**detail/action subscreen** and, compactly, as a right-aligned tag on the list row
(mirroring the scanner's `dBm`) — never a single-char badge. RAW is rendered dimmed to read
as "fallback / lower-confidence".

### Module layout

- `src/ui/components/pulse_strip.{h,cpp}` — the shared, static preview: a pure
  `pulseStripColumns(const uint16_t* durationsUs, size_t count, int width)` downsampler plus
  a thin `render(canvas, x, y, w, h)` that blits it. Shared by both screens (two call sites
  that would otherwise duplicate identical downsample+draw logic — reuse is earned, not
  speculative). Guarded like the other components so native tests link the pure function.
- `src/ui/screens/ir_record_screen.*` — add a preview on the save step (between capture and
  naming) and encoding detail on the code-action screen; surface protocol/RAW via the
  existing `describeSignal()`.
- `src/ui/screens/radio_screen.*` — the same preview on its save step and signal-action
  screen; an OOK `describeSignal` equivalent (always `RAW · N edges · <freq>`).

No new state machine, no new persistence, no change to the `.ir`/`.sub` codecs.

## Verification

- **Native unit tests** (`pio test -e native`), fail-before/pass-after on the pure logic:
  - `test/test_pulse_strip.cpp` — `pulseStripColumns()`: a known duration train maps to the
    expected column fill at a given width; a long gap stays proportionally wide; an empty or
    single-edge train yields a defined (non-crashing) result; the mapping never indexes out
    of the target width.
- **On-device operator checklist** (the display/RF boundary — HAL is manual per CLAUDE.md),
  recorded in `## As built`:
  - Capture a real remote (IR and a 433 fob) → the preview shows a structured, repeated
    train; capture ambient noise / a half-press → the preview shows a short/ragged strip and
    the operator can discard it before saving.
  - A saved Parsed IR code's detail shows `NEC · 32 bit` (or equivalent); a RAW code shows
    `RAW · N` dimmed; an OOK signal shows `RAW · N · 433.92 MHz`.
  - **Replay still actuates the target after the preview work is added** — IR bit-bang and
    OOK RMT both unaffected, confirming nothing draws during the send.

## As built

_(Filled at merge, per the Stele workflow — shipped units, deviations, on-device results,
and any deferrals.)_

**Shipped units**

- `src/ui/components/pulse_strip.{h,cpp}` — a pure `pulseStripColumns()` downsampler that
  integrates high-time into per-column duty (not a point-sample, so a burst denser than one
  pixel reads as partial fill and a long gap keeps its proportional width), plus a
  device-guarded `renderPulseStrip()` that blits it as bottom-anchored bars and draws a flat
  baseline for a degenerate train. The pure function is wired into the native build filter
  past the `-<ui/>` exclusion.
- `test/test_pulse_strip/` — 10 native cases (red-before/green-after), covering degenerate
  inputs, single-edge polarity, proportional gap width, duty fill for a dense burst, and the
  never-index-past-width guard.
- IR (`ir_record_screen`) — a `CAPTURE_REVIEW` state between capture and naming (preview +
  Save/Discard), and an encoding line + strip on the code-detail screen. `describeSignal()`
  now includes the decoded bit count (`RC6 20 bit 0x…`); RAW is dimmed with its edge count.
- Radio (`radio_screen`) — the same `CAPTURE_REVIEW` step and a detail-screen encoding line
  (`RAW · N edges · <freq>`) + strip, drawn from the signal's own first-level polarity.

**Deviations** — none. The design held: the strip is static (painted only on the review and
detail screens, never during a send), duty-fill rather than a square/sine wave, and a Parsed
IR code degrades to its protocol summary rather than fabricating edges.

**On-device results (2026-09-06, Cardputer ADV)**

- OOK 433.92 MHz capture renders the pulse strip correctly.
- A Parsed IR code (RC6, 20 bit) shows its protocol + bit-count summary and **no strip** —
  the intended Parsed-vs-RAW behaviour (a decoded frame stores no timing train; the decode
  itself is the quality signal). DoD scenario 4 confirmed.
- Replay still actuates the target after the preview work — the never-draw-during-send
  guarantee holds for both the IR bit-bang and the OOK RMT path.
- Diagnostic aside: a smart-TV remote captured nothing. Serial showed the plain decode
  **timeout** path (neither fail-loud rejection fired), and the phone-camera test proved the
  remote pairs over BLE despite having an IR LED — no IR to capture, not a defect.

**Deferrals**

- The **RAW-IR** strip (an undecoded IR frame) is not yet visually confirmed on-device: no
  IR remote on hand produces a non-decoding frame, and the smart-TV remote is BLE. Tracked
  in [LEDGER] under Hardware / caps.
