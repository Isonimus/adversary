---
id: '0125'
title: "Integrity-pin the third-party patched build ZIPs by SHA256"
type: architecture
status: accepted
date: 2026-10-26
supersedes: []
superseded_by: []
---

## Context

Two of the build inputs are not ordinary dependencies: they are patched binaries pinned by
a bare release-asset URL in `platformio.ini`, with no integrity check.

- `platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.34/platform-espressif32.zip`
  (`platformio.ini:54`, duplicated at `:117` for the release env). ~1 MB.
- `framework-arduinoespressif32-libs @ https://github.com/bmorcelli/esp32-arduino-lib-builder/releases/download/idf-release_v5.5/bruce_esp32-arduino-libs-20251205-131242.zip`
  (`platformio.ini:57`, duplicated at `:120`). 549 MB (measured).

A git tag pins a commit, but a GitHub *release asset* can be deleted and re-uploaded at the
same tag and URL: the URL does not pin the bytes. So either asset could change under us,
from an upstream account compromise or a maintainer re-upload, and every build (and every
user who flashes the factory image) would silently pull different bytes.

This matters more here than for a typical project. The bruce libs are a single-maintainer
patched arduino-esp32 build whose entire purpose is to weaken
`ieee80211_raw_frame_sanity_check` so the firmware can transmit raw deauth frames. It is an
unofficial binary doing something the stock SDK refuses to do, shipped in a tool people run
against networks. Toolchain integrity is therefore a security property, not just hygiene.

`DEPENDENCIES.lock.md` records resolved *versions* and detects resolver drift, but it holds
no content hash of these ZIPs, and a `grep` for any sha/checksum mechanism across
`platformio.ini` and `scripts/` comes back empty. GitHub's API exposes no content digest for
a release asset, so the only way to know the bytes is to fetch and hash them.

This was carried as a deferred supply-chain item (ex-`LEDGER.md`); it is a keep-not-promote
class under ADR-0022 (supply-chain audits are never promoted to public issues).

## Decision

Integrity-pin the two third-party patched ZIPs by SHA256, verified by a CI gate.

1. **Recorded hashes.** `scripts/build-zip-checksums.json` maps each pinned URL to its
   expected `"sha256:<hex>"`. This is the single machine-parsed source of truth for the
   hashes (precedent: `scripts/raw-serial-baseline.json`); `DEPENDENCIES.lock.md` prose
   points at it rather than duplicating the values.

2. **The gate.** `scripts/lint-build-zip-integrity.mjs` (zero-dependency, like the other
   `scripts/*.mjs`) with a pure, unit-tested core and a thin CLI:
   - **Set cross-check, first and network-free.** Extract every GitHub release-asset archive
     URL (`.zip` / `.tar.gz` / `.tar.xz`) from `platformio.ini`. Every such URL must have a
     checksum entry (a pinned third-party binary with no recorded hash fails the gate, so a
     pin bump cannot silently bypass it), and every checksum entry must still be referenced
     by `platformio.ini` (a stale entry fails, so dead pins are pruned). Registry `lib_deps`
     are `owner/name@version`, not URLs, so they are out of scope here and stay covered by
     the lock.
   - **Hash compare.** Only if the set cross-check passes, download each URL, stream it
     through SHA256, and compare to the recorded hash. Any mismatch fails loud with the URL
     and both digests. A failed download is retried a few times, then fails: never a
     pass-by-default on a network error.

3. **When it runs.** A standalone `build-zip-integrity` CI job mirroring `dep-lock-staleness`
   (self-test the lint, then run it), triggered **only** on a `v*` tag push or on a change to
   `platformio.ini` or `scripts/build-zip-checksums.json`. It does not run on every push.
   Measured rationale: the bruce ZIP is 549 MB, the build matrix already caches
   `~/.platformio` keyed on `platformio.ini` so normal pushes never re-fetch these bytes, and
   an every-push integrity job would be the sole 549 MB re-download each run for near-zero
   added safety (the bytes only ship at release). Release and pin-change are exactly the
   moments the bytes ship or change.

4. **Scope.** The two third-party release-asset URLs only. The Xtensa toolchain and the stock
   `framework-arduinoespressif32` ride *inside* the pioarduino platform ZIP, so verifying that
   ZIP covers them transitively, no separate entry is needed.

## Consequences

- A swapped upstream asset is caught at the two moments that matter: cutting a release (the
  factory image users flash) and changing a pin. The gate fails the release push rather than
  shipping unverified bytes.
- **Bump procedure.** When a pinned URL changes (platform or libs upgrade), re-fetch the new
  asset, recompute its SHA256, and update `scripts/build-zip-checksums.json` in the same
  commit. The set cross-check enforces that a new pin has an entry and an old one is removed;
  `DEPENDENCIES.lock.md`'s Bump/Gate section records the step.
- **Cost.** At release, the 549 MB asset is fetched twice (once by the gate, once by the
  build), acceptable for infrequent releases. Normal pushes are unaffected.
- **Residual gap (accepted).** A swap during a window in which we cut no release and change no
  pin is not detected until the next release or pin-change. This is acceptable because the
  firmware only ships on release; a weekly cron fetch of 549 MB was considered and rejected as
  cost without matching value. If drive-by detection ever becomes worth it, a scheduled run is
  an additive change, not a redesign.
- First-use trust is unchanged: recording today's hashes trusts the bytes we currently build
  and ship. The gate protects against *future* silent change of those bytes, which is the
  stated threat.
