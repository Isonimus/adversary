---
id: '0154'
title: "PMF-aware attack selection"
type: slice
status: accepted
date: 2026-11-13
supersedes: []
superseded_by: []
---

## Goal

Stop autohunt from silently burning per-target windows on PMF-required APs where injected
deauth cannot land. Cache the two PMF bits (`pmfCapable`, `pmfRequired`) on each scan
record, show a PMF marker in the scanner UI, and teach autohunt to resolve an **effective
strategy** per target from the declared intent and the target's PMF bits (ADR-0153):
`pmfRequired` with a solicitation intent runs PMKID only; `pmfRequired` with a `Deauth`
intent degrades to PMKID when the default `degrade to PMKID on PMF` toggle is on, else
emits `Skipped (PMF)` and advances. BTM steering and its frame builder are explicitly out
of this slice; they land behind the opt-in `BTM steer (unverified)` knob in a sibling
slice once there is on-device evidence to drop or keep the "unverified" label. CSA and
per-target scoring + backoff are separate ADRs and separate slices (ADR-0153 scope
boundary).

Closes the PMF-cache + effective-strategy + scanner-marker half of the LEDGER item
**PMF-aware attack selection + 802.11v BTM steering** (line 89); the BTM-builder half
stays on the ledger until its own slice lands.

## Definition of Done

- **Given** a WiFi scan result containing a PMF-required AP (RSN IE `MFPR=1`) and a
  PMF-capable-but-not-required AP (`MFPC=1`, `MFPR=0`) and a legacy WPA2 AP (both bits
  clear)
- **When** the scan completes
- **Then** each `NetworkInfo` carries `pmfCapable` and `pmfRequired` set from that AP's
  RSN IE, and the scanner-list row for the `MFPR=1` AP shows the PMF marker while the
  other two rows do not.

- **Given** autohunt is running and the next target has `pmfRequired == false`
- **When** the autohunt state machine resolves the effective strategy at `NEXT_TARGET`
- **Then** the effective strategy equals the operator's declared intent unchanged, the
  per-target line renders the intent's label (`Deauth`, `PMKID`, or `Both`), and the hunt
  log records the resolved strategy alongside the intent.

- **Given** autohunt's intent is `PMKID` or `Both` and the next target has
  `pmfRequired == true`
- **When** `NEXT_TARGET` resolves the effective strategy
- **Then** the effective strategy is `PMKID` only, deauth injection is suppressed for
  this target, the per-target line renders `PMKID (PMF)`, and the solicitation path runs
  as it would for a non-PMF target.

- **Given** autohunt's intent is `Deauth`, the next target has `pmfRequired == true`,
  and the `degrade to PMKID on PMF` toggle is on (default)
- **When** `NEXT_TARGET` resolves the effective strategy
- **Then** the effective strategy is `PMKID`, the per-target line renders `PMKID (PMF)`,
  and the hunt log records the degrade.

- **Given** autohunt's intent is `Deauth`, the next target has `pmfRequired == true`,
  and the `degrade to PMKID on PMF` toggle is off
- **When** `NEXT_TARGET` resolves the effective strategy
- **Then** the target is advanced past without transmitting on it, the per-target line
  renders `Skipped (PMF)`, and the hunt log records the skip with reason
  `PMF_DEAUTH_INEFFECTIVE`.

## Design

**What was considered.** Searched the existing WiFi scan path (`WiFiScanner::processResults`
reads `WiFi.SSID`, `WiFi.BSSID`, `WiFi.RSSI`, `WiFi.channel`, `WiFi.encryptionType`) and the
underlying ESP-IDF API (`esp_wifi_scan_get_ap_record{s,}` returning `wifi_ap_record_t`).
Searched `src/modules/capture/handshake_capture.{h,cpp}` and `src/modules/attack/` for a
strategy seam: ADR-0139's per-hunt Strategy (`Deauth`, `PMKID`, `Both`) is the natural
lift point, and the per-target `handshakeSaved_` guard already resets at `NEXT_TARGET`,
so the "effective strategy" concept can live at that boundary without a second state
track. Searched `src/modules/capture/handshake_capture.cpp` for the existing promiscuous
infrastructure: it uses `esp_wifi_set_promiscuous`, a channel-dwell tick, and a filtered
`wifi_promiscuous_pkt_t` callback, reusable for the beacon-sniff pass below. Reused:
ADR-0139's strategy enum + config row (extended with a resolver, not replaced); the
existing `NetworkInfo` struct (two new booleans; no serialization yet because no scan
snapshot is persisted); the EventBus + hunt-log emission path (one new field on the
per-target line and log row, no new event); the promiscuous-mode plumbing already proven
in handshake_capture. Deliberately not reused: a fresh scan mechanism; `WiFi.scanNetworks`
keeps running for the normal discovery flow, and the beacon sniff is an addendum next to
it. Deliberately *not* done: no refactor of the strategy enum into a bitmask (two values
do not clear the rule-of-three bar); no persistence of PMF bits outside the live scan
record (follow-up if a scan snapshot ever ships).

**Where PMF comes from (probed 2026-11-13).** ADR-0153's Decision sketch assumed the RSN
IE was already in hand after a scan. Reading the pinned libs showed that is wrong for
this toolchain: `wifi_ap_record_t` in IDF 5.5 (bruce_esp32-arduino-libs, 2025-12-05 build,
`esp_wifi_types_generic.h` line 314-343) carries `authmode`, `pairwise_cipher`,
`group_cipher`, phy flags, HE-AP info and bandwidth, and nothing about PMF; the only
`wifi_pmf_config_t` in the header is on the STA side (how we advertise PMF when we join
an AP). The scan API discards the beacon RSN IE buffer after building the record, so
neither `esp_wifi_scan_get_ap_record` nor the Arduino `WiFi.*` shim can return the two
bits. This blind spot is now one of the three concrete motivations on the LEDGER "own a
shared patched ESP-IDF + Arduino-core libs" item; until that build exists, PMF has to be
read off the raw frame.

**Mechanism.** Add a short promiscuous beacon-sniff pass that runs **after**
`WiFi.scanNetworks()` completes, before `processResults` publishes `WIFI_SCAN_COMPLETED`:

- After the normal scan builds the `NetworkInfo` list with PMF bits defaulted to
  `false`, `WiFiScanner` enters promiscuous mode via the existing
  `wifi_utils::enablePromiscuous(cb, &filter)` helper and `managementFrameFilter()`
  (already in `src/utils/wifi_utils.h`, in use by handshake_capture). The type-level
  filter `WIFI_PROMIS_FILTER_MASK_MGMT` catches all management subtypes; the RX
  callback discards everything except beacon (subtype 8) and probe-response (subtype 5)
  with a two-byte check. No new helper is needed and no WiFi-mode flip is needed:
  `WiFiScanner::processResults` runs with the radio already in STA mode, scan state
  machine idle (proved by the handshake_capture precedent and the Arduino shim's
  post-scan state).
- Iterate the distinct channels appearing in the scan result's BSSIDs. For each channel,
  `esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE)` and dwell up to
  `PMF_SNIFF_DWELL_MS` (default 120 ms, tunable; typical home beacon interval is ~100
  ms). For every beacon received, look up the BSSID in the scan result; if its PMF
  bits are not yet filled, parse the RSN IE out of the frame body and store the two
  bits. Advance past a channel as soon as every BSSID on it has been filled.
- Parsing lives in a pure free function
  `parseRsnPmfBits(const uint8_t* ieBuf, size_t ieLen, bool* outMfpc, bool* outMfpr)`
  in `src/utils/`. The parser walks the RSN IE (id `0x30`): skip the 2-byte version
  and 4-byte group cipher suite, skip the pairwise cipher suite list (count +
  count*4 bytes), skip the AKM suite list (count + count*4 bytes), read the 2-byte RSN
  Capabilities field, return bit 7 (MFPC) and bit 6 (MFPR). Returns `false` and leaves
  outputs untouched on any truncation, mismatched IE id, or malformed length. Pure
  function, native-testable with fixture byte arrays.
- A WPA2 or legacy AP with no RSN IE (open, WEP, pure WPA) gets `pmfCapable =
  pmfRequired = false` by default, which is correct: PMF is defined only inside RSN.
- An AP never heard from during the sniff pass (hidden-SSID edge case, or moved out of
  range between scan complete and sniff start) also keeps the defaults. That is honest
  best-effort: the resolver treats `pmfRequired == false` as "attack as declared,"
  which is the safe fallback from the operator's point of view (we try; we do not
  silently skip).
- After the sniff pass, exit promiscuous, publish `WIFI_SCAN_COMPLETED` as before. No
  change to the event payload; PMF is read off `NetworkInfo` by any consumer that
  cares.
- Expected cost: `distinct_channels * PMF_SNIFF_DWELL_MS`. A dense suburban scan tops
  out around 6-8 distinct 2.4 GHz channels, so ~720-960 ms of added scan time in the
  worst case. Scans are user- or autohunt-triggered; no UI flow is on a tight scan
  deadline. If the added latency hurts on-device, the dwell is a `constexpr` knob in
  one place.

**Effective-strategy resolver.** New pure free function
`resolveEffectiveStrategy(intent, pmfRequired, degradeToPmkid)` returning
`EffectiveStrategy { Deauth, PMKID, Both, Skipped }` plus a reason code for the log
(`NONE`, `PMF_DEGRADED`, `PMF_DEAUTH_INEFFECTIVE`). Called once at `NEXT_TARGET` and
latched for that target's window; the handshake-capture run loop reads the effective
strategy, not the intent, so deauth and solicitation arms honour the gate naturally.
Pure function, native-testable, no ESP dependency. Lives in `src/utils/` next to the RSN
IE parser.

**Degrade toggle.** Added to the Auto Hunt config form as a `ConfigRow` under `Strategy`,
default on, labelled `Degrade on PMF: On|Off` (matches the destructive-verb UI
convention of one verb per action; "degrade" is the honest word for what happens).
Stored on `AutoHuntConfig` alongside `strategy`; no new storage, settings serializer
tracks it under the existing autohunt settings block.

**Scanner UI marker.** The scanner-list row renders a compact `[PMF]` chip next to the
security label on rows where `pmfRequired` is true. The marker is advisory: the operator
can still select the AP manually (manual Handshake, deauth, evil twin), which keeps the
non-autohunt flows unchanged. PMF-capable-but-not-required is not surfaced today;
`MFPR` is the actionable bit, `MFPC` is kept on the record for later use.

**Hunt-log extension.** The per-target hunt log row grows a resolved-strategy field.
ADR-0139's existing log holds intent; after this slice it holds `intent -> effective`
when the two differ and `intent` alone when they match, keeping the common-case row
readable.

**Out of scope.** BTM frame builder and `BTM steer (unverified)` knob (sibling slice;
needs the field-verification pass before the knob ships). CSA injection (future sibling
ADR, scope boundary set in ADR-0153).
Per-target scoring + exponential backoff (own ADR and own slice; the resolver may later
consume `pmfRequired` as one input to that scorer, but the two concerns do not merge
here).

## Verification

- **Native unit test, new.** `test/test_pmf_resolver.cpp`: all nine cells of the
  resolver's truth table (`{Deauth, PMKID, Both} x {pmfRequired=false, pmfRequired=true
  degrade=on, pmfRequired=true degrade=off}`). Proven fail-before (inlining the resolver
  to return the intent unchanged fails every `pmfRequired=true` row) and pass-after.
  Wired into the native `build_src_filter` and run through `pio test -e native`.
- **Native unit test, new.** `test/test_rsn_ie_parser.cpp`: fixture RSN IE byte arrays
  for (a) WPA2-PSK no PMF, (b) WPA2-PSK MFPC-only, (c) WPA2-PSK MFPC+MFPR, (d) WPA3-SAE
  MFPR, (e) truncated IE at every field boundary (version, group, pairwise count,
  pairwise suites, AKM count, AKM suites, capabilities), (f) wrong IE id. Asserts the
  two output bits for the valid cases and `false` return with untouched outputs for the
  truncation and bad-id cases. Proven fail-before (zeroing the RSN Capabilities read in
  the parser fails cases b/c/d) and pass-after. Wired into the native suite.
- **Native unit test, new.** `test/test_wifi_scanner_pmf.cpp`: feed the scanner's
  `setMockNetworks` path three fixture records and separately drive `parseRsnPmfBits`
  with their synthesized beacon IE buffers; assert the merge step lands the two bits on
  the right `NetworkInfo` by BSSID. Covers the merge logic without needing a live
  radio.
- **Native unit test, existing.** `pio test -e native -f test_handshake_capture` stays
  green; the effective-strategy integration adds one new case asserting a
  `pmfRequired=true` + `Deauth` intent with degrade off produces no deauth frames for
  the window.
- **Native suite.** `pio test -e native`: full suite green, whole-suite count grows by
  the new tests only.
- **Build.** `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both build
  clean. Builds serial, never concurrent (per CLAUDE.md note on shared `~/.platformio`
  state).
- **On-device, Cardputer ADV (deployment target).** With a PMF-required neighbour AP in
  range (phone hotspot forced WPA3-SAE, or any modern router with PMF required): the
  scanner row shows `[PMF]`; an autohunt with intent `Deauth` + degrade-on logs
  `PMKID (PMF)` for that target; with degrade-off logs `Skipped (PMF)` and advances
  without any deauth bursts visible on a sniffing companion. Record the added scan
  latency (beacon-sniff pass) on a typical 6-8 channel neighbourhood against the
  pre-slice baseline, and confirm it stays within the budget stated in Design.
- **On-device, M5StickC Plus2.** Scanner shows `[PMF]` on the same test AP (gesture
  path, no new input needed).

## As built

<!-- Filled in at merge. -->
