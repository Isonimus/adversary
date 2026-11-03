---
id: '0137'
title: "Clientless PMKID via injected association, gated on an on-device feasibility spike"
type: architecture
status: accepted
date: 2026-11-03
supersedes: []
superseded_by: []
---

## Context

v0.7.2 fixed the *passive* PMKID path (commit 0d1b9a9): when a real client (re)associates
to the target, forced by deauth if needed, the AP's EAPOL M1 carries a PMKID KDE and
`extractPmkidFromEapol` now reads it from the correct offset, keyed on the client MAC the
AP addressed M1 to. That path still needs a client to be present, or deauthed into
reconnecting.

The clientless ideal is a crackable PMKID with no client present and no deauth: solicit M1
from the AP ourselves. The PMKID in M1 is `HMAC-SHA1(PMK, "PMK Name" | AP_MAC | STA_MAC)`,
so if we are the station the AP addresses M1 to, the PMKID is keyed on our own MAC and the
hashcat `.22000` STA MAC is ours (not a real client's).

Current architecture, measured in `handshake_capture.cpp`: `start()` sets `WIFI_AP_STA` and
raises a hidden softAP (`adversary_hs`) purely to own a TX netif for `esp_wifi_80211_tx`;
all RX is through the promiscuous callback, and deauth/disassoc frames are injected on that
TX path. The module never associates to anything. It is a passive sniffer with a raw-frame
transmitter bolted on.

Two mechanisms could make the AP send M1 to us:

- **A. Managed STA association** (`esp_wifi_connect` with bogus credentials). The driver
  completes 802.11 auth + association at the MAC layer (hardware ACKs handled) and the AP
  begins the 4-way, sending M1. The problem is extraction: the precompiled bruce
  wpa_supplicant runs the 4-way internally and exposes no raw-M1 / PMKID callback, and
  promiscuous mode does not run concurrently with a managed connection on this driver to
  sniff it. So the PMKID never reaches us. This is the same precompiled-IDF opacity ADR-0135
  hit: a supported-looking hook the shipped archive does not actually expose.

- **B. Injected association** (hcxdumptool-style): stay in promiscuous mode and inject an
  802.11 authentication frame, then an association request carrying an RSN IE, from a chosen
  source MAC, and read the AP's auth-response, association-response and EAPOL M1 off the same
  promiscuous RX we already use. This composes with the current architecture with no mode
  switch: it is the deauth TX path plus two new frame builders and a small request/response
  state machine. The load-bearing unknown is whether the AP emits M1 to an association our
  radio does not hardware-ACK (promiscuous TX has no auto-ACK for the spoofed STA MAC), which
  is AP- and timing-dependent and unproven on the ESP32-S3 + precompiled IDF.

## Decision

Pursue clientless PMKID via **injected association (Option B)**, layered on the existing
promiscuous-RX + softAP-TX path, and **gate the whole feature behind an on-device
feasibility spike before any production code or UI ships**.

Rationale for B over A: the PMKID only has value if it reaches `recordPmkid`, and the
managed path (A) cannot surface it through the precompiled supplicant. B keeps the single
radio in promiscuous mode throughout, so it reuses the frame-injection and capture plumbing
already in `HandshakeCapture` instead of introducing a connect/promiscuous mode conflict.

Design, when built:

- A solicitation state machine on top of the current capture loop: inject auth-request,
  await auth-response, inject association-request (RSN IE advertising a PMKID-capable AKM),
  await association-response and M1. No managed connect, no leaving promiscuous mode.
- Source MAC is a chosen station MAC we control; `recordPmkid` records it as the STA (the
  correct hashcat value in this path, unlike the passive path where it is the real client).
- Reuse the softAP TX netif already raised for deauth; solicitation adds frame builders, not
  a new interface.
- Bounded timeout/retry: an attempt is N auth+assoc retries with a per-attempt timeout; no
  M1 after the retries reports no-PMKID and stops. It does not fall back to deauth silently,
  because the point of this mode is to work without deauth.
- Keep the new logic pure and host-tested like `pmkid_utils`: the auth-request and
  association-request frame builders and the state-machine transitions live under `utils/`
  with native regression tests; the device shell only does TX/RX.

### Acceptance condition (must hold before the feature is built out)

The one unproven assumption is that a PMKID-capable AP emits EAPOL M1 to an injected,
un-ACKed association on this chip and IDF. This **must be proven on-device with a spike**
before the mode and its UI are written: against a known PMKID-emitting AP, inject the auth +
association-request sequence and confirm the promiscuous path captures an M1 whose RSN IE
carries a PMKID KDE, that `recordPmkid` stores it keyed on our chosen STA MAC, and that the
saved `.22000` line cracks (hashcat/pwncrack). This is the ADR-0135 lesson applied up front:
a precompiled-IDF interaction is not trusted until a forced on-device test shows it firing.
Host tests cover the frame builders and the state machine; they cannot cover whether the AP
answers. Tracked in `LEDGER.md`, not promoted to an issue (hardware-verify, bus-factor-1).

## Consequences

- If the spike passes: implement the solicitation state machine and a "Clientless PMKID"
  start path in the handshake UI, with native tests for the frame builders and transitions.
  The deauth-forced passive path (v0.7.2) stays as the fallback for APs that do not answer
  solicitation.
- If the spike fails (the AP never emits M1 without a hardware-ACKed association on this
  stack): the clientless ideal is not reachable on this hardware/IDF. Record the negative
  result and keep the passive deauth-forced path. Do not ship a mode that silently captures
  nothing. Reopen only with a different stack (for example the owned-IDF build already
  tracked under build-reproducibility) or different hardware.
- No new radio interface and no connect/promiscuous mode conflict: the feature is additive
  TX on the path that already injects deauth, so it does not disturb the passive capture
  behaviour.
- The hashcat STA MAC in this path is our own injected MAC, which is correct and differs
  from the passive path; the two paths must not share the "STA = client" assumption.
