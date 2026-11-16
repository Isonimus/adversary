---
id: '0159'
title: "Quiet client stimulation via QoS-Null Data"
type: architecture
status: accepted
date: 2026-11-16
supersedes: []
superseded_by: []
---

## Context

Deauth-force (ADR-0094, slice-0094) is the hammer the handshake-capture flow swings when the
operator wants a 4-way handshake out of a target: knock the client off the AP with a spoofed
deauth, catch the EAPOL M1..M4 as it reassociates. It is loud, it is logged, and under PMF
(ADR-0153, slice-0154) it does not reach the client at all. Clientless PMKID solicitation is
the current degraded path for PMF-required targets, and BTM steering (slice-0155/0156) is the
honest "may work, may not" knob alongside it.

None of these three paths wake a client that is simply idle or in power-save without being a
visible, management-layer event the target's stack and any nearby WIDS will record. For a
handshake flow that wants to be quieter than deauth, or that wants to try something before
deauth on a PMF-capable-but-not-required target, there is no intermediate option today.

QoS Null-Data frames (type=2, subtype=12) are the tool published tooling reaches for here
(0ldev/Politician and derivatives). They are data frames, so 802.11w does not protect them:
the MFP check that drops a spoofed deauth under PMF does not apply to Null-Data. The target's
association state is moved by the 802.11 PWR_MGT bit in the frame control field, so a
client-impersonating burst that toggles PWR_MGT between 1 (buffer frames at the AP) and 0
(flush buffered frames to the client) can force the AP to release whatever it has pending for
that client, including a queued group-key rekey. On some drivers a sustained burst also tears
the association down as an anti-flood response, triggering a real reassociation and a real
4-way.

Three mechanisms exist in the wild and are not equivalent:

1. **AP-to-client QoS-Null wake-up**: spoof the AP's BSSID, send Null-Data at the client. Wakes
   a power-saving STA briefly; rarely surfaces EAPOL on its own.
2. **Spoofed PWR_MGT toggle (client-to-AP)**: spoof the client's MAC, alternate PWR_MGT=1/0 at
   the AP. Forces AP to buffer, then flush; surfaces pending GTK rekey traffic when one exists.
3. **Sustained client-to-AP Null-Data burst**: #2 at rate, counting on the AP's anti-flood or
   the driver's keep-alive timer to treat the client as misbehaving and tear the association
   down, forcing a fresh 4-way.

Only (3) is plausibly deauth-equivalent in effect, and even then it is implementation-dependent
across vendor firmware. The ADR must pin one and document what the device actually tries.

The frame itself is tiny: a 26-byte QoS data header (24-byte data header + 2-byte QoS Control)
with FromDS=0/ToDS=1, subtype=QoS-Null, and a zero-length frame body. There is no RSN, no
fragmentation, no retry logic the attacker cares about. Byte-exact construction is a pure
function of (client MAC, AP BSSID, PWR_MGT bit, sequence number); TX is an
`esp_wifi_80211_tx` call on the active interface with the active channel. This is the same
shape as the BTM builder/TX split that slice-0155 and slice-0156 established, and the pattern
should hold here for the same reason: byte layout is testable natively, TX is not.

## Decision

**Add QoS-Null Data stimulation as a third capture-time attack mode, alongside Deauth and
Clientless, default off, operator-picked. The device implements mechanism (3) above
(sustained client-to-AP Null-Data burst with alternating PWR_MGT) as the single attempted
behaviour; the other two mechanisms are not implemented.**

Frame construction:

- New `src/utils/qos_null_frame.h` exposes a pure byte-exact builder
  `build_qos_null_data(uint8_t* out, size_t out_len, const uint8_t ta[6], const uint8_t
  bssid[6], bool pwr_mgt, uint16_t seq_no) -> size_t` that writes a 26-byte QoS Null-Data
  frame (type=2, subtype=12) with FromDS=0, ToDS=1, PWR_MGT set per argument, RA=BSSID,
  TA=client MAC, DA=BSSID (BSS A3), QoS Control=0x0000 (TID 0, no ack policy override), and
  zero payload. Returns written length (always 26) or 0 on invalid buffer. Pure, host-testable,
  no ESP-IDF includes.
- Device-only `src/utils/qos_null_tx.cpp` wraps the builder with `esp_wifi_80211_tx`, exposing
  `qos_null_tx::send_burst(const uint8_t ta[6], const uint8_t bssid[6], size_t count,
  uint32_t inter_frame_us) -> esp_err_t`. The burst alternates PWR_MGT 1/0 across frames so
  each pair encodes one buffer-flush cycle. Sequence numbers increment per frame. Channel is
  the caller's responsibility (handshake capture already parks on the target's channel).

Addressing is pinned to the mechanism-(3) triplet:

- TA (Transmitter Address, A2 field) = target client MAC (spoofed)
- RA (Receiver Address, A1 field) = target AP BSSID
- A3 (BSSID field, for ToDS=1 the destination address inside the BSS) = AP BSSID
- FromDS=0, ToDS=1 (client to AP direction)

Burst shape defaults, tunable only if measurement on hardware demands it:

- 20 frames per burst, 10 pairs of PWR_MGT=1 then PWR_MGT=0.
- 10 ms inter-frame interval (100 frame/s rate), so a burst takes 200 ms wall-clock.
- One burst per second during the active window; the handshake sniffer runs continuously,
  and capture succeeds as soon as any valid 4-way lands, so the burst cadence is
  "frequently enough that the AP's keep-alive timer sees pressure, infrequently enough that
  the sniffer gets quiet gaps to decode in".

UI placement:

- The capture-time config form (handshake Auto Hunt and Manual) grows a `QoS-Null` toggle
  row alongside the existing `Deauth` and `Clientless` toggles. All three are independently
  on/offable; the default for the new row is off. No menu entry is added under attacks: QoS-Null
  is strictly a capture stimulation technique, not a standalone attack, and surfacing it
  outside the capture flow invites misuse against targets that will not yield.

Interaction ordering within a capture window:

- If multiple stimulation modes are enabled for the same target, the device runs them in a
  fixed order per window: **deauth first** (the loudest and most-proven path), **QoS-Null
  second** after the deauth sub-window elapses without an EAPOL M1, **BTM third** if the
  target is `pmfRequired` and the BTM steer knob is on. Clientless (PMKID solicitation) runs
  in parallel with all three, since it is a request-response the AP either answers or does
  not, independent of client state. The ordering is fixed, not operator-configurable, so the
  capture log reads the same across runs and after-action review is comparable.

PMF interaction, stated honestly:

- QoS-Null is a data frame. 802.11w does not protect data frames, so the frame itself is
  never rejected under PMF-required. This is the specific value: it is the one stimulation
  path that is not gated by the PMF bit cached on the scan record (slice-0154).
- "Not protected" is not the same as "effective". The AP's response to repeated unauth
  Null-Data is implementation-dependent, and enterprise WIDS routinely flag unassociated
  Null-Data streams from unknown MACs as spoofing. The ADR records that we try it; whether
  it works on any given target is a measurement question the device cannot answer in
  advance.

Scope boundary:

- **No auto-fallback.** The device does not automatically pick QoS-Null when PMF=required and
  the operator has selected Deauth only. That path exists (effective-strategy resolution,
  ADR-0153) for the deauth-to-PMKID degrade; extending it to QoS-Null is a second decision
  this ADR does not make. If measurement shows QoS-Null landing EAPOL on PMF-required targets
  where PMKID solicitation does not, a follow-up ADR turns on the degrade.
- **No AP-side mechanism.** Mechanisms (1) and (2) above are not implemented. Their value is
  lower (rarely surfaces EAPOL alone) and their frame construction differs enough that
  supporting them is a separate builder, not a parameter on this one. A follow-up slice can
  add them if evidence suggests they help on targets where (3) does not.
- **No new dependency on the adv-libs archive.** `esp_wifi_80211_tx` already works for
  arbitrary data frames on the pinned platform (slice-0158); only management-frame TX needed
  the `ieee80211_raw_frame_sanity_check` WEAK override. QoS-Null is a data frame, so it rides
  the unpatched path. Lock snapshot is unchanged.

## Consequences

- One pure builder + one device TX helper, matching the BTM pattern (`btm_frame.h` /
  `btm_tx.cpp`). Byte-exact tests on native; on-device verification is the follow-up slice's
  job, not the ADR's.
- The handshake capture config form grows one toggle row. Existing operators who do not touch
  the row see no behavioural change; the new path is strictly additive.
- Capture logs now record which stimulation modes fired and in what order per window, so
  after-action review can tell "deauth worked" from "deauth timed out, QoS-Null worked" from
  "neither worked, PMKID carried it". This requires a small extension to the capture-event
  record format, parallel to how ADR-0153 added effective-strategy to the hunt log.
- The device sends ~20 Null-Data frames per second per target during an active QoS-Null
  window. A WIDS-equipped site will log this; the operator picked a quieter alternative to
  deauth, not an invisible one. The what's-new line and the UI knob label should carry this
  honesty rather than frame QoS-Null as stealth.
- Mechanism (3) is the one the device attempts. If field measurement shows it rarely or
  never surfaces a 4-way on real targets, this ADR gets a follow-up that either pins a
  different mechanism or retires the knob. The device never claims a capability it has not
  demonstrated: the UI label is `QoS-Null stim`, not "quiet deauth replacement".
- PMF-required targets gain a second path that is not blocked by 802.11w (PMKID solicitation
  was the only one before). Whether the path yields EAPOL is a measurement question; whether
  the frame reaches the AP at all is answered: yes, because the frame is unprotected by
  design of the standard.
- No new transitive library risk, no new precompiled-archive dependency, no change to the
  lock snapshot. Heap cost is one 26-byte stack buffer per TX call; no persistent state.
