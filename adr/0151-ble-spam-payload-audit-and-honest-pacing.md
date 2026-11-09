---
id: '0151'
title: "BLE spam audit: randomized Fast Pair pool, one honest pacing model, per-provider effectiveness"
type: architecture
status: accepted
date: 2026-11-09
supersedes: []
superseded_by: []
---

## Context

The BLE spam path (`ble_spanner.cpp`, `buildPayload()` / `updatePayload()` / `runSpamCycle()`)
advertises rotating pairing-prompt payloads across five providers: APPLE, ANDROID (Fast Pair),
WINDOWS (Swift Pair), SAMSUNG (EasySetup), and CAROUSEL (rotates the first four). Each cycle
swaps the random static address and the payload. The code works, but three defects have
accumulated, and this release ("HID device types + BLE spam polish") is where they get fixed.

A current-OS effectiveness survey (2026-11-09) frames what is worth spending effort on:

- **Windows Swift Pair** is the most persistent vector: still fires the connect prompt on
  Windows 10/11 whenever the "Swift Pair notifications" setting is on. The payload we send
  (`{0x06,0x00,0x03,0x00,0x80}` + name, i.e. Microsoft company ID `0x0006`, Swift Pair beacon)
  is correct and stays.
- **Android Fast Pair** is throttled, not closed: a popup for a given model ID is suppressed
  by a per-device cooldown (community-measured ~15 min). Our per-cycle MAC rotation already
  defeats the *per-device* cooldown (each cycle is a fresh device), but Android also bloats
  and rate-limits when one advertiser cycles too many *distinct* model IDs. The current pool
  is a fixed 4 IDs rotated sequentially, and an in-code `NOTE` already flags the fix.
- **Apple proximity pairing** (the AirPods "nearby action" type `0x07`, our `PAIRING` prompt)
  still produces occasional popups under Apple's post-iOS-17.2 cooldowns. But the **SourApple**
  action-type prompts (`APPLE_ID_MODAL`, `SOFTWARE_UPDATE`, `TRANSFER_PHONE`, `GUIDED_ACCESS`,
  and the `C1`-flag crash vector, CVE-2023-42941) were mitigated in iOS 17.2 (Dec 2023): they
  no longer crash and rarely surface on current iOS. The code still offers them as if they
  were live. Presenting a dead vector as a working one is the same dishonesty ADR-0150 removed
  from the HID appearance.

Two concrete code defects:

1. **Static Fast Pair pool.** `updatePayload()` rotates a hardcoded 4-entry `profiles[]`
   sequentially. No diversity across sessions, and no protection against the cache-bloat
   limiting above.

2. **Two conflicting pacing formulas for one knob.** "Intensity" (1-10) drives the cadence
   twice, incoherently:
   - `runSpamCycle()`: `interval = 1100 - intensity*100`, floored at 200 ms. This is the
     *rotation period* (how often the MAC + payload swap), and it is what actually paces
     distinct popups, because each rotation is a new device.
   - `setIntensity()`: `interval = 500 - (level-1)*50`, floored at 20 ms, written to the
     radio *advertising interval* (min/max).
   The two curves disagree (at intensity 10: 200 ms rotation vs a 20 ms adv interval), and the
   radio interval is the wrong lever anyway: a tighter adv interval re-sends the *same* device
   faster, which does not create new popups and only costs airtime/heat. The rotation period is
   the one that matters.

## Decision

### 1. Randomized Fast Pair subset per session

Expand `profiles[]` to a vetted pool of well-known Fast Pair model IDs, and at `start()` draw a
**random subset of 4** into an active rotation (sequential rotation within the subset is kept,
so every chosen ID is used before repeat). Re-roll the subset each `start()`. This keeps any one
session advertising few distinct IDs (beats cache-bloat limiting) while varying which IDs appear
across sessions (beats staleness). Subset size 4 and the draw are the only new logic; the
per-cycle rotation and payload bytes are unchanged. The folded-in "dynamic 3-4 model selection"
backlog item is satisfied here.

### 2. One pacing model: intensity sets the rotation period only

Intensity maps to the **rotation period** through a single formula, used by `runSpamCycle()`.
The radio advertising interval is set once to a fixed fast value (20-40 ms) and is no longer a
function of intensity: `setIntensity()` stops writing the adv interval and only stores the level
(the rotation loop reads it). This removes the second, fighting formula. Rationale is measured
in the Context: the rotation period, not the adv interval, is what produces distinct popups.

### 3. Per-provider effectiveness is labelled, not implied

Each provider/prompt carries a static effectiveness tag (`LIVE`, `THROTTLED`, `LEGACY`) derived
from the survey, surfaced in the BadBLE UI so the operator sees what a target will actually do:

- Windows Swift Pair: `LIVE`.
- Android Fast Pair: `THROTTLED` (works with rotation; cooldowns apply).
- Apple `PAIRING`: `THROTTLED`.
- Apple SourApple action prompts (`APPLE_ID_MODAL`/`SOFTWARE_UPDATE`/`TRANSFER_PHONE`/
  `GUIDED_ACCESS`): `LEGACY` (patched iOS 17.2; kept for pre-17.2 devices).
- Samsung EasySetup: `THROTTLED` (unconfirmed on current One UI; not claimed `LIVE`).

The tag is advisory copy, not a gate: every provider stays selectable (old devices exist, and
the field operator decides). The honesty is the point, consistent with ADR-0150.

### Out of scope (logged, not done here)

Real-device popup confirmation (does each provider still fire on a current iPhone / Pixel /
Windows 11 on the bench) is hardware-verify, bus-factor-1, and stays a LEDGER item. The
effectiveness tags above are the best current public evidence, not bench-confirmed by us.

## Consequences

- Android sessions advertise a varying 4-of-N model set: fewer throttle events per session,
  more variety across sessions. The pool and subset draw are pure and native-tested.
- "Intensity" becomes a single, predictable curve (rotation period); the radio interval is a
  fixed constant. One formula, one meaning. Native-tested on the mapping.
- The operator sees a per-provider effectiveness tag and is no longer misled into expecting a
  crash/popup from a patched Apple action vector. Dead vectors are labelled, not removed.
- A hardware-verify LEDGER item carries the only claim we cannot settle in-repo: that the
  `LIVE`/`THROTTLED` tags hold on current bench hardware.
