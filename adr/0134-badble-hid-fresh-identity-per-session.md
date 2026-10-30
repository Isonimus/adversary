---
id: '0134'
title: "BadBLE HID: fresh random identity per session, persistence opt-in via fixedBleMac"
type: architecture
status: accepted
date: 2026-10-30
supersedes: []
superseded_by: []
---

## Context

BadBLE's HID keyboard mode advertises on a **fixed identity**: a public Bluetooth address
(the factory MAC by default, or the operator's `wireless.fixedBleMac` when set) plus the GAP
name `"Universal Key"`, with Just-Works bondable security (`ble_spanner.cpp`,
`setOwnAddrType(BLE_OWN_ADDR_PUBLIC)` and `setSecurityAuth(bond=YES, MITM=NO, SC=YES)`). The
fixed public address was chosen in-code "for caching/bonding" so a host would recognise and
silently reconnect the keyboard across sessions. That choice was never recorded in an ADR.

It produces a confirmed, reproducible failure (ledgered 2026-09-08):

1. A host pairs with the device and stores an LTK keyed to {fixed public MAC, "Universal Key"}.
2. The device is re-flashed. Re-flashing wipes the device's NVS, so the device loses its half
   of the LTK. The host keeps its half.
3. On next BadBLE start the device advertises the **same** fixed identity. The host recognises
   it as the bonded keyboard and auto-reconnects, immediately attempting to encrypt the link
   with the stored LTK.
4. The device has no matching key, SMP encryption fails, the link drops, NimBLE auto-re-
   advertises, the host auto-reconnects again: an endless reconnect/disconnect loop. The only
   known recovery is forgetting "Universal Key" on the host, which then re-pairs and stays.

The key fact the earlier fix ideas missed: **the stale key lives on the host, not the device.**
Two ledgered options do not fix it. `deleteAllBonds()` on start is a no-op in the reflash case,
because after a reflash the device already holds zero bonds; it cannot clear the host's copy.
Dropping to Just-Works-no-store has the same hole: an already-bonded host still presents its old
LTK on reconnect and still fails. Neither touches the root cause, which is that the device keeps
presenting a **stale-matchable identity** the host tries to encrypt against.

A red-team HID injector connects, types a payload, and is done. Cross-session reconnect
persistence (the reason the fixed identity existed) buys that workflow almost nothing, while the
fixed identity is exactly what makes the stale-bond collision possible.

## Decision

**Default BadBLE HID to a fresh random identity per session, and make persistence opt-in.**

1. **Fresh random address by default.** When `wireless.fixedBleMac` is unset (the default), HID
   mode advertises on a freshly generated random address each start, instead of the factory
   public MAC. A host has no bond for a never-seen address, so it always pairs fresh and the
   stale-LTK collision cannot occur. The host's old bond for a previous identity stays unused
   and harmless.

2. **Persistence is opt-in via `fixedBleMac`.** When the operator explicitly sets
   `wireless.fixedBleMac`, the device honours it as a fixed public address (the old behaviour),
   for the operator who deliberately wants a stable, reconnectable identity and accepts that a
   reflash then requires forgetting the device on the host. The setting's help text states that
   caveat.

3. **Loop-breaker regardless of identity mode.** Track reconnect churn: if the HID link drops to
   an unencrypted/failed-auth state repeatedly within a short window, stop auto-re-advertising
   and surface a status ("re-pair needed: forget this device on the host") rather than spinning.
   This bounds the opt-in fixed-MAC case, which can still hit the stale-bond collision, and
   guards against any other reconnect storm. The churn test is a pure helper in `src/utils/` so
   it is unit-tested in the native build (BadBLE itself is device-only, excluded from native).

## Consequences

- The default BadBLE HID session no longer collides with a host's stale bond, so the endless
  reconnect loop cannot happen on a default-configured device. No host-side "forget" is needed
  in the common case.
- Cross-session silent reconnect is lost by default. This is acceptable for the inject-and-go
  HID workflow and is recoverable by setting `fixedBleMac`.
- The opt-in fixed-MAC path retains the original collision risk after a reflash, now bounded by
  the loop-breaker and documented in the setting's caveat rather than presenting as a hang.
- `fixedBleMac` changes meaning from "override the MAC" to "opt into a stable, persistent
  identity". Operators who had set it keep their behaviour; operators who had not get the new
  fresh-identity default.
- The churn detector is pure and host-tested; the device code feeds it connection events.
