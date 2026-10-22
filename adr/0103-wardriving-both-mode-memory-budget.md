---
id: '0103'
title: "Wardriving BOTH-mode memory: flatten the dedup slope, don't purge the canvas"
type: architecture
status: accepted
date: 2026-10-21
supersedes: []
superseded_by: []
---

## Context

BOTH-mode wardriving (WiFi + BLE concurrently, ADR-0060) OOM-reboots in the field. An
instrumented run (`-DWARDRIVE_HEAP_TRACE`, 705 samples) measured the failure instead of
guessing it (a 2026-10-15 code-only guess had not resolved it):

- **Baseline is thin**: at 0 devices, BOTH mode has only **27.3 KB free internal**. The NimBLE
  controller (~58.6 KB, ADR-0060) and the ~32 KB RGB332 canvas are co-resident with WiFi on
  the ~205 KB no-PSRAM board.
- **Free declines linearly at ~84 B/device**, to 5.8 KB at 253 devices; the reboot came at
  min 3.0 KB free / 1.5 KB largest-block, during a 247->253 new-device burst that needed a
  contiguous block larger than the largest free one.
- **Ruled out**: a cyclic leak (consumption tracks unique-device *count*, not scan cycles) and
  fragmentation-collapse (largest-block/free ratio is stable ~0.6 start to end; the largest
  block declines *with* free, it does not shatter while free stays high).
- **Ruled in**: O(unique-devices) growth against a thin budget. The ~84 B/device exceeds the
  `seenBSSIDs_` `unordered_set<uint64_t>` node (~24 B incl. overhead) alone, so there is a
  secondary per-device cost, most likely the NimBLE controller's duplicate-filter cache
  (it accumulates seen BD_ADDRs in controller RAM), plus fragmentation.

This corrects an earlier framing (the LEDGER/diagnosis note and the "canvas purge is the
biggest lever" claim): the **slope is the mechanism**, not the baseline by itself. 27.3 KB
would be survivable with a flat slope, because the per-cycle transients are only ~1-3 KB and
fit comfortably against the 18 KB largest block seen at session start. The budget only runs
out because the slope eats it down to the fragmentation floor. It is WiFi+BLE-specific because
WiFi-only has ~58 KB more headroom (no NimBLE) and a slower slope (no BLE devices, no
controller dup-cache).

A blunt fix (free the 32 KB canvas during the session via
`SystemManager::prepareForMemoryIntensiveTask`) was considered and rejected: `Canvas` is an
off-screen `M5Canvas` sprite that every screen draws into, and the purge *deletes* it, so the
display would freeze on its last frame for the whole multi-minute session. Acceptable for a
few-second TLS upload, not for a live scan where the operator watches counts climb.

## Decision

Fix the slope, not the baseline. Two phases, neither of which touches the UI:

1. **Bounded dedup.** Replace the unbounded `unordered_set<uint64_t>` with a **fixed-size
   Bloom filter** keyed on the existing 64-bit `observationKey(mac, type)`, sized to the
   dedup's existing per-session ceiling `MAX_NETWORKS = 5000` so one constant governs
   capacity. At p = 1%, m = ceil(-n ln p / (ln2)^2) = 47926 -> 48000 bits (6000 B) and
   k = round((m/n) ln2) = 7 hashes. That is a *fixed* ~6 KB slab versus the `unordered_set`'s
   ~112 KB at 5000 devices (impossible on this budget), and it holds flat instead of
   declining. `getNetworkCount()` stays exact by reusing the existing `session_.networksFound`
   counter (incremented on each genuinely-new add; the Bloom cannot count), so no second
   counter is introduced. The hard `MAX_NETWORKS` cap is kept, now as the Bloom's design load:
   past it the false-positive rate climbs, so new adds stop with a **one-time fail-loud
   warning** ("dedup at capacity, coverage degraded") rather than silently dropping devices at
   a rising miss rate. The honest cost: a Bloom false positive classifies a *new* device as
   already-seen and drops it, so the error mode is a rare missed device (not a duplicate row).
   At the design load this is ~1%, within wardriving's inherent probabilistic coverage (RSSI
   and drive-by geometry already miss devices). **Fail loud, not silent**: track the set-bit
   fill ratio and, when it exceeds the design load (so the real FP rate would climb above
   target), surface a one-time "dedup saturated, coverage degraded" warning so the operator
   knows the session outgrew the filter rather than silently losing devices.

2. **NimBLE duplicate handling, as an *isolated, measured* step, not bundled with phase 2.**
   The idea is to scan with duplicates *allowed* at the controller so its duplicate-address
   cache stops growing with unique advertisers, relying on our now-bounded Bloom instead.
   But disabling the controller filter makes `BLEScanner::onResult` fire for *every*
   advertisement, not just the first per device, and that callback allocates transient
   `std::string`s (name, manufacturer data) and does a linear dedup scan each time, so it
   could *add* churn and fragmentation and offset the benefit. Whether the controller cache is
   even the unbounded culprit (vs a fixed-size exceptional list) is unverified. So phase 3 is
   not shipped with phase 2: we prove phase 2 first, then toggle phase 3 alone and compare
   traces, attributing its effect instead of shipping both and not knowing which did what. It
   is a one-line change (`NimBLEScan::setDuplicateFilter(false)`), trivially revertible.

**Phase 1 (baseline) is deferred and conditional.** It is a baseline lever, not a slope lever,
so it is only reached for if a flat slope still proves too thin. If so, raise the baseline by a
**responsiveness-preserving** method, shrinking the sprite to the dynamic region or rendering
that region directly to the panel, never by deleting the buffer and freezing the screen. The
~6 KB Bloom slab does not spend this baseline: the manager is a function-local-static singleton,
so the filter lives in `.bss`, not the heap, and the measured cold baseline stayed at 27.3 KB
after phase 2 (see Validation).

The rejected LRU-ring alternative (a fixed ring of recent MAC hashes, whose error mode is a
benign duplicate CSV row rather than a dropped device) costs ~3x the memory of the Bloom for
the same device ceiling; on a budget whose thinness is the entire problem, that is
unaffordable, and WiGLE dedups server-side so the Bloom's missed-device cost is the one worth
paying here.

The `WARDRIVE_HEAP_TRACE` instrument stays until this fix is field-proven: it is the harness
that verifies the slope flattened. Its removal and the largest-block heap-badge feature are a
separate follow-up (see LEDGER), sequenced *after* the proof run.

## Consequences

- The per-device slope drops from ~84 B/device to ~30 B/device (measured, see Validation),
  which moved the OOM ceiling from ~253 to ~490 devices but did **not** flatten it. The residual
  ~30 B/device is term 2 (the NimBLE controller dup-cache), so phase 3 is required, not optional,
  to reach the Bloom design load (`MAX_NETWORKS` = 5000) with its fail-loud at-capacity warning.
- Dedup gains a tunable false-positive knob and loses exactness; `getNetworkCount()` stays
  exact by reusing `session_.networksFound` (no second counter). A new native regression test
  covers the Bloom (no false negatives; FP rate within target at the 5000-key design load),
  where the pure Bloom logic lives (kept out of the device-only scan glue, like the existing
  `formatWigleRow` split).
- The canvas and the render path are untouched, so the live wardriving screen stays
  responsive; the operator's concern that drove this ADR is resolved by *not* purging.
- Verification is measured, not asserted: the field trace confirmed the slope halved (not that
  it flattened). The instrument that found the bug measured the fix, and corrected this ADR's
  pre-run prediction that phase 2 alone would flatten the slope (see Validation).
- Follow-up remains open: NimBLE footprint reduction (scan-only config) as a further baseline
  lever if ever needed, and the instrument cleanup + setting-gated largest-block badge.

## Validation

Proof run on 2026-10-22 (`-DWARDRIVE_HEAP_TRACE`, BOTH mode on the Cardputer, trace preserved
to `scratchpad/wardrive-heap-2026-10-22.csv`). The instrument's log is append-only across
reboots, so the pre-fix 2026-10-21 trace is an exact prefix of the file; the post-fix data is
the tail (one session that resumed across two OOM reboots, device count climbing 311 -> 333 ->
490 before the final reboot).

| Metric | Pre-fix (unordered_set) | Post-fix (Bloom) |
|---|---|---|
| Cold baseline @ 0 devices | 27.3 KB free | 27.3 KB free (unchanged: Bloom is in `.bss`) |
| Per-device slope | ~84 B/device | ~30 B/device |
| OOM ceiling (reboot) | ~253 devices | ~490 devices |
| Heap at crash | 3.0 KB free / 1.5 KB largest | 2.0 KB free / 1.1 KB largest |

Phase 2 removed ~54 B/device, more than the `unordered_set` node alone (~24 B), confirming the
set was also fragmenting the heap through rehash/node churn. The result **falsifies this ADR's
pre-run prediction** that phase 2 alone would flatten the slope near ~21 KB: it halves the slope
and roughly doubles the device ceiling, but the OOM persists at a higher count. The residual
~30 B/device is term 2 (the NimBLE controller duplicate-address cache), which is now phase 3's
measured target rather than a hypothesis. The crash recorder confirmed the reboots (PANIC under
memory pressure) but stores only reason plus heap stats, no backtrace, so the heap trace, not
the crash log, is what localized the fault.
