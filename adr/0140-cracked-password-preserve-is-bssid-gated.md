---
id: '0140'
title: "Cracked-password preservation across re-save is BSSID-gated; same-BSSID PSK rotation is an accepted limitation"
type: architecture
status: accepted
date: 2026-11-04
supersedes: []
superseded_by: []
---

## Context

When a capture is saved for a network we have already cracked, the save path preserves the
CRACKED status and password rather than resetting it: a re-capture of a known network should
not throw away the password we already obtained. The 4-way path did this; the PMKID path did
not, and extending it raised a correctness question (discussed while building ADR-0138/0139):
does preserving a cracked password leave stale data if the AP's PSK changed since we cracked it?

Two facts shape the answer:

- **There is no on-device WPA PSK verification.** `wifi_password.h` only generates a random
  dashboard-AP key; there is no PBKDF2 / PMK / MIC code in `src/`. Cracking is entirely
  off-device (wpa-sec / pwncrack), and the password is downloaded into the metadata. So the
  ideal "preserve only if the password still validates against the fresh capture" is not
  cheaply available: it would mean implementing WPA key derivation from scratch.
- **The 4-way preserve matched by SSID only.** Two different APs sharing an SSID (common on
  public and corporate names) could make one AP's fresh capture inherit the other's cracked
  password. That wrong-data path is more likely than PSK rotation and was latent.

## Decision

Preserve a CRACKED password across a re-save on **both** the 4-way and PMKID paths, **gated on
a BSSID match**: the password is carried over only when the re-captured AP has the same BSSID
as the stored record (shared helper `lookupCrackedForResave`, keyed by SSID through
`getMetadata`, then BSSID-checked). `crackedAt` is retained as provenance.

Rationale, against the two rejected alternatives:

- **Not "preserve by SSID only" (the old 4-way behaviour):** the BSSID gate closes the
  SSID-collision wrong-password bug, which is the most likely source of a stale/incorrect
  password and is fully detectable, unlike rotation.
- **Not "remove preserve, always reset to NOT_UPLOADED":** this is a frequently-offline field
  device. Resetting on every re-save makes it forget passwords it already has until the next
  successful wpa-sec/pwncrack sync, which may be hours away or never. Throwing away good
  results to guard against a rare event is the worse default.

**Same-BSSID PSK rotation remains an accepted, documented limitation.** If the operator rotates
the PSK on the same AP between our crack and a re-capture, the preserved password is stale and
we cannot detect it on-device. The operator resolves it by deleting the capture to force a
re-crack. A real fix needs on-device WPA PSK verification, which would be its own ADR.

## Consequences

- `saveHandshakeToPcap` and `savePMKIDToFile` share `lookupCrackedForResave(bssid, ssid)`; the
  4-way path gains the BSSID gate it lacked, and the PMKID path gains preservation at parity.
- Networks never cracked are unaffected (the lookup returns nothing, status stays as computed).
- The staleness window is bounded to same-BSSID PSK rotation and is surfaced only by the
  retained `crackedAt` timestamp until (if ever) on-device verification exists.
