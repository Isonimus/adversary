---
id: '0160'
title: "Quiet client stimulation via QoS-Null Data burst"
type: slice
status: accepted
date: 2026-11-16
supersedes: []
superseded_by: []
---

## Goal

Ship ADR-0159: a third capture-time stimulation path alongside Deauth and Clientless,
default off, where the device impersonates the target client and emits a short burst of
QoS Null-Data frames to the AP with the 802.11 PWR_MGT bit alternating. The frame is a
data frame, so 802.11w does not protect it; on a PMF-capable-but-not-required target this
is a quieter alternative to deauth, and on a PMF-required target it is the one
stimulation path besides PMKID solicitation whose transmission is not gated by the MFP
bit. Closes the LEDGER `[feature]` **Quiet client stimulation via QoS-Null Data** entry
(line 28), which already names the files this slice touches and records ADR-0159 as its
design gate.

The slice does not make QoS-Null an auto-fallback for `pmfRequired` deauth-only intents:
ADR-0159 pins "no auto-fallback" as an explicit scope boundary, and extending the
effective-strategy resolver (slice-0154) to pick QoS-Null on its own is a second decision
a future ADR owns if measurement warrants it.

## Definition of Done

- **Given** a byte-level native test of the pure frame builder with a fixed TA, BSSID,
  `pwr_mgt` bit, and sequence number
- **When** `build_qos_null_data(out, 26, ta, bssid, pwr_mgt, seq)` is called
- **Then** the returned length is 26, the Frame Control type bits encode data (type=2) and
  subtype encodes QoS Null (subtype=12), `FromDS=0`/`ToDS=1`, the PWR_MGT bit in FC byte 2
  matches the argument, A1 (RA) equals BSSID verbatim, A2 (TA) equals the client MAC
  verbatim, A3 equals BSSID verbatim, the sequence control encodes the argument sequence
  number with fragment=0, and the 2-byte QoS Control at offset 24 is `0x0000` (TID 0, no
  ack policy override).

- **Given** the QoS-Null toggle on for an autohunt or Manual-mode handshake target, a
  valid client MAC known for the target, and the configured per-window burst budget
- **When** the capture window's QoS-Null sub-window fires (after the deauth sub-window,
  before BTM per ADR-0159 ordering)
- **Then** the device emits a 20-frame burst at 10 ms spacing (~200 ms wall-clock) whose
  PWR_MGT bits alternate 1/0/1/0... across the burst and whose sequence numbers increment
  monotonically, and the hunt-log line for that target gains a `QoSNull:injected` marker.

- **Given** the handshake Auto Hunt and Manual config forms on either device
- **When** the operator scrolls to the QoS-Null row
- **Then** the row label reads literally `QoS-Null:` as a `ConfigRow` with `On|Off`
  values, the default rendered value is `Off`, and the row sits between the Deauth and
  Clientless toggles (slice-0109 placement conventions preserved).

- **Given** a single capture window where Deauth, QoS-Null, and BTM are all enabled and
  all three fire conditions are reachable
- **When** the window runs
- **Then** the hunt log records fired markers in the fixed order `Deauth` then
  `QoSNull:injected` then `BTM:injected` within the window, never reordered by operator
  configuration (Clientless remains parallel and unordered against the three).

- **Given** the QoS-Null toggle off (the default)
- **When** any capture window runs on any target
- **Then** no QoS-Null frame is ever emitted, regardless of PMF state or whether a client
  MAC is known.

- **Given** no client MAC is known for the target (the handshake sniffer has not yet
  observed any associated STA on the BSSID)
- **When** the QoS-Null sub-window would fire
- **Then** the device logs `QoSNull:skip_no_client` once per window and emits no frame;
  the sub-window still elapses so BTM's half-window predicate is unaffected.

## Design

**What existed and was considered.** The BTM slice pair (slice-0155 / slice-0156)
established the pattern this slice copies, directly and without variation:

- Pure byte-exact builder as a header-only file under `src/utils/`
  (`src/utils/btm_frame.h` is the precedent). Builder returns written length; native
  tests pin every header byte.
- Thin device TX helper as a `.cpp` beside the header (`src/utils/btm_tx.cpp` is the
  precedent), a no-op shim on native so the host test runner stays radio-free.
- Pure predicate function in the header, with every policy cell under native test, so the
  live call site in `handshake_screen.cpp` reads a predicate that is already covered by
  the test suite even though the screen itself is not reachable from the native suite.
- Per-target latch cleared at `NEXT_TARGET`, mirroring `btmInjectedThisTarget_`.

Nothing in `src/modules/attack/deauth.*` is reused: QoS-Null is a different frame class
(data, not management), with different addressing (unicast client→AP, not broadcast), and
a different TX cadence (sustained short burst, not a per-interval stream). Nothing in
`src/utils/btm_frame.h` or `src/utils/btm_tx.cpp` is reused either; the builders differ
in every byte after the Frame Control type/subtype, and sharing a header would collapse
two honest layouts into one false abstraction. The slice-0154 `pmf_resolver` is read for
the latched `pmfRequired` bit if a future ADR turns on auto-fallback, but this slice does
not extend the resolver and does not consult the bit for policy: QoS-Null fire condition
is PMF-independent on purpose, per ADR-0159.

**Frame layout.** The 26-byte QoS Null-Data frame is built in one shot:

```
Frame Control    2 bytes  byte 0 = 0xC8 (version=0, type=2 Data, subtype=12 QoS Null)
                          byte 1 = ToDS=1 FromDS=0 MoreFrag=0 Retry=0 PwrMgt=<arg>
                                   MoreData=0 ProtectedFrame=0 Order=0
Duration/ID      2 bytes  little-endian, 0x0000 (AP is free to override)
Address 1 (RA)   6 bytes  target AP BSSID
Address 2 (TA)   6 bytes  target client MAC (spoofed)
Address 3        6 bytes  target AP BSSID (DA inside the BSS for ToDS=1)
Sequence Ctrl    2 bytes  little-endian, fragment=0, sequence=<arg>
QoS Control      2 bytes  little-endian, 0x0000 (TID 0, no ack policy override)
```

No frame body. Total 26 bytes. The builder is pure, header-only, no ESP-IDF includes;
returns 26 on success and 0 on a buffer too small or a null output pointer.

**Burst shape.** The TX helper
`qos_null_tx::send_burst(const uint8_t ta[6], const uint8_t bssid[6], size_t count,
uint32_t inter_frame_us)` loops `count` times, alternates `pwr_mgt` on parity of the
iteration index, increments a static per-TX sequence counter, builds a fresh buffer each
iteration, calls `esp_wifi_80211_tx(WIFI_IF_STA, ...)`, and `vTaskDelay`s
`inter_frame_us` microseconds between frames. Defaults from ADR-0159: `count=20`,
`inter_frame_us=10000`. Channel is the caller's: handshake capture already parks on the
target's channel before firing any attack sub-window, same contract Deauth and BTM
already rely on.

**Fire condition and ordering.** The handshake capture state machine already runs one
attack sub-window per capture window. Ordering before this slice (effective after
slice-0156): `Deauth` first (if enabled), `BTM` second (if enabled, at the half-window
mark). This slice inserts `QoSNull` between them: deauth sub-window elapses, then the
QoS-Null sub-window runs its one burst, then BTM becomes eligible at the half-window
mark. Clientless (PMKID solicitation) continues to run parallel to all three without
contesting for the TX path's single frame-at-a-time contract (PMKID solicitation is an
EAPOL-start request that rides the normal association path, not a raw-TX injection).

The pure predicate:

```
bool shouldFireQosNullBurst(bool qosNullEnabled,
                            bool qosNullAlreadyFiredThisTarget,
                            bool deauthSubWindowElapsed,
                            bool clientMacKnown);
```

returns true iff all four are true. Everything the live call site needs to know is in the
predicate; the test suite covers all 16 cells. The `clientMacKnown` input reads a new
field on the latched target state (`latchedClientMac_` plus a `latchedClientMacKnown_`
bool), populated by the same handshake sniffer that already observes STA↔AP traffic for
the capture; when no STA has ever been seen for the BSSID in this window, the burst is
skipped with `QoSNull:skip_no_client` so the operator sees the honest reason.

**UI placement.** The handshake Auto Hunt and Manual config forms both gain one row,
between the existing `Deauth` and `Clientless` rows. The row label is `QoS-Null:`, the
`ConfigRow` values are `On|Off`, and the default is `Off` (ADR-0159). The row is not
surfaced in any menu outside the handshake flow: QoS-Null is a capture stimulation
technique, never a standalone attack, and a menu entry would frame it as one.

**Non-scope.**
- No auto-fallback from `pmfRequired` deauth-only intents to QoS-Null. The resolver
  (slice-0154) is read but not extended. A follow-up ADR owns that decision if
  measurement warrants it (ADR-0159, "No auto-fallback").
- No AP-side QoS-Null mechanism. ADR-0159 explicitly rejects (1) and (2) from the
  mechanism survey: only (3), the sustained client-to-AP PWR_MGT-toggle burst, is
  implemented.
- No new `adv-libs` dependency, no new `ieee80211_raw_frame_sanity_check` override. The
  override from slice-0158 is for the management-frame path (deauth); `esp_wifi_80211_tx`
  accepts arbitrary data frames on the unpatched path without intervention. The
  `DEPENDENCIES.lock.md` snapshot is unchanged by this slice.
- No PMKID solicitation changes. Clientless remains independent and parallel.
- No CSA injection and no multi-BSS decoy work (separate LEDGER items, their own ADRs).

## Verification

- `pio test -e native -f test_qos_null_frame` — new native test module asserting:
  - Builder returns 26 for the sunny path; returns 0 for `out_len < 26`; returns 0 for
    `out == nullptr`.
  - FC byte 0 is exactly `0xC8` (version=0, type=Data=2, subtype=QoS-Null=12).
  - FC byte 1 has `ToDS=1`, `FromDS=0`, all other FC2 bits zero when `pwr_mgt=false`;
    PWR_MGT bit (bit 4 of FC byte 1) toggles with the argument.
  - A1 equals BSSID verbatim, A2 equals TA verbatim, A3 equals BSSID verbatim.
  - Sequence Control encodes the argument sequence number with fragment nibble 0,
    little-endian.
  - QoS Control at offset 24 is `0x0000`.
  - All 16 predicate cells of `shouldFireQosNullBurst` match the DoD scenario: fires in
    exactly the one cell (enabled & not-yet-fired & deauth-elapsed & client-known), false
    in the other 15.
- `pio test -e native` — full suite stays green; expected delta is the new
  `test_qos_null_frame` module's count added to the previous baseline (recorded in
  `## As built` at merge).
- `pio run -e cardputer` and `pio run -e m5stick` — both build (serial, per CLAUDE.md).
- `node scripts/lint-docs.mjs` — green with the new slice document added.
- On-device (lab): flash Cardputer ADV, enable QoS-Null in Manual mode handshake config
  against a known lab target with a known associated client MAC, confirm from the serial
  trace that one 20-frame burst is emitted after the deauth sub-window elapses, that the
  hunt-log line carries `QoSNull:injected`, and that toggling the knob off eliminates all
  QoS-Null transmission. Whether the burst elicits a 4-way is implementation-dependent per
  ADR-0159 and is called out as an honest open question tracked under `## As built`
  follow-ups, not a slice-blocking criterion.

## As built

<!-- Filled in at merge. -->
