---
id: '0001'
title: "Shelve LoRa features; keep GNSS compatibility and pin definitions"
type: architecture
status: accepted
date: 2026-09-02
supersedes: []
superseded_by: []
---

# ADR-0001 — Shelve LoRa features; keep GNSS compatibility and pin definitions

## Context

The Cap LoRa 1262 (SX1262) was integrated in commit `d233172` (2026-06-24): the
`LORA_NSS/RST/BUSY/IRQ` and `CAP_GPS_RX/TX` pin definitions in `src/config/pins.h`, the
`TARGET_CARDPUTER` NSS-high block in `src/main.cpp`, and the NSS de-selection in
`src/hal/storage/sd_manager.cpp`.

A later working session decided to "remove LoRa." Its transcript was lost to Claude Code's
30-day retention before it produced any commit, and the decision was then mis-recorded in
`ISSUES.md` as a wholesale removal task (`git revert d233172` minus the SD pull). That
framing is wrong, and reverting on it would break real capabilities:

- The operator physically **swaps caps** depending on the capability needed at the moment —
  the LoRa cap and the CC1101/NRF24 multi-radio cap share the same top-side header and are
  never attached at once. The LoRa cap's **GNSS** (ATGM336H over `CAP_GPS_RX/TX`) is still
  used for wardriving.
- The pre-SD-init **NSS de-select is load-bearing regardless of any LoRa feature**: a
  floating SX1262 chip-select drives MISO and corrupts SD mount. It is an SD-safety
  invariant, not LoRa code.

What was actually shelved was narrower: the LoRa **RF feature work** (SubGHz/mesh and the
like) and its **carousel menu entry** — not the cap's GNSS support, and not the pins.

## Decision

- LoRa **RF features are not developed** for now: no LoRa carousel entry, no SubGHz/mesh work.
- The LoRa cap's **GNSS support is retained and supported** (`CAP_GPS_*`, the ATGM336H NMEA
  probe).
- **All `LORA_*` and `CAP_GPS_*` pin definitions in `pins.h` stay.**
- The pre-SD-init **NSS de-select stays as an SD-safety invariant**, independent of LoRa.

## Consequences

- `pins.h` intentionally keeps `LORA_*` defs with no LoRa feature UI. These deliberately
  overlap the multi-radio cap's defs (only one cap is attached at a time); that overlap is a
  hardware fact, documented at the defs.
- The mis-recorded "Remove the Cap LoRa 1262 support" backlog item is **void** and is not
  carried into `LEDGER.md`: acting on it would break SD mounts and GNSS.
- Any future LoRa feature work requires a **new ADR superseding this one**, stating why
  reopening it is right.
- The SD-safety de-select is shared with the multi-radio cap's chip-selects (both must be
  de-asserted before SD mount); that coexistence is tracked as the multi-radio cap work.
