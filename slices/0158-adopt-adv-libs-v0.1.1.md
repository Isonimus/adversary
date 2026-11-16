---
id: '0158'
title: "Adopt esp32-arduino-adv-libs v0.1.1 and retire the weaken_deauth link hook"
type: slice
status: accepted
date: 2026-11-16
supersedes: []
superseded_by: []
---

## Goal

Swap both device envs' `framework-arduinoespressif32-libs` pin from the third-party
Bruce precompiled blob (`bmorcelli/esp32-arduino-lib-builder@idf-release_v5.5/bruce_esp32-arduino-libs-20251205-131242.zip`)
to the owned `Isonimus/esp32-arduino-adv-libs@v0.1.1/adv-esp32-arduino-libs-v5.5-20261115-e68e301.zip`
drop-in, and retire the now-redundant `weaken_deauth` build-time link hook. This closes the
`[feature]` LEDGER item (line 47) that tracked the adoption, moves this repo's RF-stack
provenance off a third-party maintainer's release cadence, and removes one of two concrete
failure modes called out in the parallel-build warning in CLAUDE.md.

The owned build (repo `Isonimus/esp32-arduino-adv-libs`, release v0.1.1) is a lock-driven
overlay rebuild that ships `libnet80211.a` with `ieee80211_raw_frame_sanity_check` *weak*
in the archive itself. The archive is byte-identical in patch posture to the Bruce blob
for everything else; the weak-symbol state is now a property of the archive, not a
post-download `objcopy --weaken-symbol` applied on every build by
`scripts/weaken_deauth_pre.py` + `scripts/weaken_deauth_symbol.sh`. Successful link
without the hook is itself the proof the archive is correctly patched: a strong symbol
would collide with `deauth.cpp`'s override at link time with a multiple-definition error.

v0.1.1 vs v0.1.0: identical `upstreamBuilderCommit` (`e68e301…`), identical
`patchStackHash` (empty — both patches are lock-driven overlay ops, not source patches),
identical `resolvedIdfCommit`/`resolvedArduinoCommit`; only the builder image digest and
`builtOn` timestamp moved (v0.1.0 cut 11:32 UTC, v0.1.1 re-cut 15:15 UTC, same day
2026-11-15). Adopting v0.1.1 picks the current provenance line; v0.1.0 would work too.

## Definition of Done

- **Given** the new Isonimus v0.1.1 `framework-arduinoespressif32-libs` pin in both
  `[env:cardputer]` and `[env:m5stick]`, and `[env_embedded]`'s `extra_scripts =
  pre:scripts/weaken_deauth_pre.py` line removed
- **When** `pio run -e cardputer` and (separately, serial per CLAUDE.md) `pio run -e m5stick`
- **Then** both builds succeed with no link-time multiple-definition error on
  `ieee80211_raw_frame_sanity_check` and no reference to any `weaken_deauth` script in the
  build log, which proves the archive itself ships the symbol weak.

- **Given** the retirement of the link hook
- **When** `grep -r weaken_deauth` is run over the repo
- **Then** no file references the retired scripts: `scripts/weaken_deauth_pre.py` and
  `scripts/weaken_deauth_symbol.sh` are deleted, and CLAUDE.md's parallel-build warning
  no longer cites "the `weaken_deauth` pre-hook fails to resolve objcopy" as the specific
  failure mode (the parallelism rule itself stays — the shared-package-installer race is
  real with or without the hook; only one cited symptom changes).

- **Given** the pin change crosses a platform-package release-ZIP URL
- **When** CI runs the `build-zip-integrity` gate (ADR-0125)
- **Then** `scripts/build-zip-checksums.json` carries an entry for the new v0.1.1 asset
  with `sha256:844810ba7725142c3f7d5121bf954af6fdb80c8b33ca09c99be0aa8e0c3df64f` and no
  entry for the retired Bruce zip; the gate passes.

- **Given** the lock snapshot is the ADR-0125 audit trail
- **When** `DEPENDENCIES.lock.md` is read after the bump
- **Then** the "Last verified" date is 2026-11-16, the native test count is the current
  one (recorded in `## As built`), and the declared-pins table's `framework-arduinoespressif32-libs`
  row cites Isonimus v0.1.1 rather than the Bruce 2025-12-05 zip.

- **Given** the retired hook is gone
- **When** the Cardputer ADV is lab-flashed with the new build and a one-target deauth is
  run from Manual mode
- **Then** the deauth frame transmits as before (observable in the serial trace and at the
  target), confirming that the archive-level weak symbol survives in the shipped firmware
  the way the former `objcopy` pass did. This is the honest smoke test for "the symbol is
  actually weak" that the build alone cannot distinguish from "the strong override was
  silently dropped by the linker as redundant."

## Design

**What existed and was considered.** The current pin
(`bmorcelli/esp32-arduino-lib-builder/releases/download/idf-release_v5.5/bruce_esp32-arduino-libs-20251205-131242.zip`)
is a third-party precompiled archive layered with a build-time `objcopy --weaken-symbol`
in `scripts/weaken_deauth_pre.py` (PlatformIO pre-action) + `scripts/weaken_deauth_symbol.sh`
(the actual objcopy call), chained through the `extra_scripts` entry in
`[env_embedded]`. The chain exists because a framework-libs reinstall/update can silently
restore the symbol to strong, breaking the link with "multiple definition" on
`ieee80211_raw_frame_sanity_check`; running the idempotent weakener on every build
guarantees the symbol is weak before the link step.

The new pin brings the weak symbol into the archive itself. The LEDGER's cross-repo
initiative (line 47, 2026-11-15 status) names the repo, the release, and the exact
one-step adoption: swap the URL, delete the hook, delete the wiring. No other code
changes are required: no src/* is touched, no tests are touched, no HAL change, no
platform/arduino-core change. Both envs' `platform =`
(`pioarduino/platform-espressif32@55.03.34`) and `platform_packages =` (the arduino-libs
URL being swapped) remain structurally identical; only the one URL differs.

**Why reuse nothing.** There is nothing in src/* to reuse: this is a toolchain-blob
provenance swap plus a build-script retirement. The only files that change are
`platformio.ini` (3 lines across two envs plus 2 lines in `[env_embedded]`),
`scripts/build-zip-checksums.json` (one entry replaced), and script deletions. The
`DEPENDENCIES.lock.md` snapshot gets its "Last verified" + table row refreshed, the
`CLAUDE.md` parallelism paragraph drops the hook citation, the ADR-0012 scripts table
gains an `## Amendment` block noting the row is historical (ADR-0019 immutability keeps
the table itself untouched), and the LEDGER entry is deleted. No new abstraction, no
new helper, no new test.

**Why v0.1.1 and not v0.1.0.** Both carry the same patch stack (empty source-patch
count; both overlay ops applied inside the builder). v0.1.1 is a clean rebuild with a
newer builder image digest, 4 h after v0.1.0. Either would adopt; picking v0.1.1 keeps
provenance on the newest line and costs nothing.

**What stays as an open question.** The CLAUDE.md parallel-build warning cites two
failure modes: the shared-package-installer race (the real race, which remains) and the
specific "pre-hook fails to resolve objcopy" symptom (which disappears). The rule stays;
only the cited symptom changes. The serial-build discipline is still enforced by the
host's ~3.6 GiB-free budget regardless.

**Non-scope.**
- The Cardputer ADV board target (`cardputer_adv`). CLAUDE.md flags the pin-matrix
  conflict as "dormant" on real hardware (the ADV has the I2C keyboard); adding a
  proper ADV env is a separate tracked follow-up, not something the libs swap unblocks
  on its own.
- The Sapper repo's `EXT_RAM_BSS_ATTR` static-attribute PSRAM placement supersession.
  Patch #2 (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`) ships in v0.1.1 and is
  inert here (this project has no PSRAM board); the matching supersession of
  `esp32-wifi-sapper:ADR-0080` is that repo's to make.
- Roadmap patches #3/#4/#5 (scan-record PMF bits, supplicant PMKID path, panic-hook
  wrap). Each was probed and withdrawn after measurement; no owned-build overlay is
  coming (see `esp32-adv-libs:ADR-0019`, `-0021`, `-0023`, `-0024`, `-0025`). Not in
  scope here either.

## Verification

- `pio run -e cardputer` — clean build with no `weaken_deauth` log line and no
  multiple-definition error. (Successful link is the weak-symbol proof.)
- `pio run -e m5stick` — same, serial per CLAUDE.md.
- `pio test -e native` — full suite stays green (no src/* change expected; this is a
  toolchain-blob swap).
- `grep -rn weaken_deauth .` over the repo — only this slice document should mention it
  historically; `scripts/weaken_deauth_pre.py` and `scripts/weaken_deauth_symbol.sh`
  are gone, `platformio.ini` no longer sets `extra_scripts`, `CLAUDE.md`'s parallel-build
  paragraph drops the citation, and `adr/0012-public-release-squash-and-curation.md`
  carries only an `## Amendment` block flagging the table row as historical.
- `sha256sum` on the fetched v0.1.1 zip matches
  `844810ba7725142c3f7d5121bf954af6fdb80c8b33ca09c99be0aa8e0c3df64f`, and
  `node scripts/lint-build-zip-integrity.mjs` passes.
- On-device (lab): flash Cardputer ADV at the new pin, boot, run one Manual-mode
  deauth against a disposable target, confirm the frame transmits in the serial trace
  and (if a second device is handy) that the target client drops. This is the
  "archive-shipped weak symbol survives the actual link" end-to-end check that a
  linker-only pass cannot distinguish from "the override was silently dropped".

## As built

<!-- Filled in at merge. -->

Shipped as three commits on `feature/slice-0158-adopt-adv-libs-v0.1.1`:

- `3229044 docs(slice): adopt esp32-arduino-adv-libs v0.1.1 and retire weaken_deauth link
  hook (slice-0158)` — this document, INDEX regenerated.
- `181ee5e build(platform-pkgs): adopt esp32-arduino-adv-libs v0.1.1, retire weaken_deauth
  hook (slice-0158)` — the actual swap. `platformio.ini`: Bruce URL replaced by
  `https://github.com/Isonimus/esp32-arduino-adv-libs/releases/download/v0.1.1/adv-esp32-arduino-libs-v5.5-20261115-e68e301.zip`
  in both `[env:cardputer]` and `[env:m5stick]`; `[env_embedded]`'s `extra_scripts =
  pre:scripts/weaken_deauth_pre.py` line removed. `scripts/build-zip-checksums.json`:
  Bruce entry replaced by the v0.1.1 asset at
  `sha256:844810ba7725142c3f7d5121bf954af6fdb80c8b33ca09c99be0aa8e0c3df64f`.
  `scripts/weaken_deauth_pre.py` (51 lines) and `scripts/weaken_deauth_symbol.sh`
  (94 lines) deleted. 4 files changed, 12 insertions, 155 deletions.
- `9b390f2 docs(slice-0158): roll lock snapshot, amend ADR-0012, drop weaken_deauth
  citation` — `DEPENDENCIES.lock.md` "Last verified" rolled to 2026-11-16 with the
  current native count (1107/1107) and the Isonimus v0.1.1 provenance line replacing
  "bmorcelli patched libs"; `adr/0012-public-release-squash-and-curation.md` gained an
  `## Amendment — 2026-11-16` block flagging the `scripts/weaken_deauth_*` table row
  and the "Narrowed rule" prose as historical claims (ADR-0019 keeps the row + prose
  untouched); `CLAUDE.md`'s parallel-build warning dropped the "pre-hook fails to
  resolve objcopy" citation (the rule stays — the shared-package-installer race is real
  with or without the hook; only one cited symptom changed). The LEDGER `[feature]`
  line (previously line 47, under "Build reproducibility / dependencies") was deleted
  in the same slice; LEDGER is gitignored locally.

Per-DoD run-through:

1. **Both device builds succeed with the new pin and no `weaken_deauth` log line.** Met.
   `pio run -e cardputer` finished SUCCESS in 6m43s (flash 76.0%, 2538743 bytes — down
   from slice-0156's 76.6% baseline; the owned build's component-slim fix shaved a
   touch). `pio run -e m5stick` finished SUCCESS in 6m39s (flash 79.4%, 2497507 bytes,
   also marginally smaller than the 79.5% baseline). No "[weaken_deauth]" line
   appeared in either build log. The link succeeded with `deauth.cpp`'s strong
   `ieee80211_raw_frame_sanity_check` override against the archive, which is the
   archive-level weak-symbol proof: a strong symbol in the archive would have collided
   with "multiple definition" at link time.
2. **`grep -rn weaken_deauth` finds only historical mentions.** Met. Current build/config
   files are clean: `platformio.ini`, `CLAUDE.md`, and `scripts/` carry no reference to
   the retired hook. The surviving hits are all frozen historical prose:
   `adr/0012-public-release-squash-and-curation.md` (the original table row + "Narrowed
   rule" sentence + this slice's `## Amendment` block), `adr/INDEX.md` (the title of
   this slice), and `slices/0014-release-firmware-artifacts.md` +
   `slices/0027-weaken-objcopy-from-toolchain-env.md` (historical slice docs, ADR-0019
   keeps them). This is the desired state: the retired scripts no longer exist, and
   every surviving mention is honestly historical.
3. **`build-zip-integrity` gate passes.** Met.
   `node scripts/lint-build-zip-integrity.mjs` reports `2 pinned ZIP(s) match their
   recorded SHA256.` for the v0.1.1 asset at `844810ba7725142c3f7d5121bf954af6fdb80c8b33ca09c99be0aa8e0c3df64f`
   plus the pioarduino platform zip at `16ba6095...` (unchanged). No stale Bruce entry
   remains.
4. **Lock snapshot is current.** Met. `DEPENDENCIES.lock.md` "Last verified" is
   `2026-11-16: native 1107/1107, cardputer + m5stick builds SUCCESS` and the
   platform-packages block cites the Isonimus v0.1.1 drop-in by name with the
   weak-symbol posture. The resolved libs version string stays
   `5.5.0+sha.8410210c9a` (same upstream ESP-IDF commit; the owned build rebuilds the
   same tree with the two lock-driven overlay patches), so no other row in the
   resolved tree moved.
5. **Lab smoke test passes.** Met. Cardputer ADV flashed 2026-11-16 at 2539152 bytes
   over `/dev/ttyACM0` (46 s, hash verified). From the bench-reported trace: a Deauth
   run against SSID `GESTIONET_movil` (BSSID `28:77:77:FB:37:3F`, ch 11) emitted
   `DEAUTH: WSL bypass ENABLED - deauth frames allowed` and `DEAUTH: Started on
   channel 11, BSSID: 28:77:77:FB:37:3F`, and the on-screen deauth + disassoc counters
   advanced. The "WSL bypass ENABLED" line is only emitted by the deauth module's own
   strong-override path, so its presence is end-to-end evidence that the override
   linked against the archive-shipped weak symbol and survived into the shipped
   firmware — the one thing a successful build alone can't distinguish from "the
   override was silently dropped as redundant." The module's standard hedge ("ESP32-S3
   may block deauth frames in SDK") also prints unconditionally on every start; it is
   not evidence of a runtime block. One stray `[E][STA.cpp:530] disconnect(): STA
   disconnect failed! 0x3001: ESP_ERR_WIFI_NOT_INIT` is framework-emitted noise from
   `start → stop → deauth-restart` sequencing and predates this slice (same upstream
   ESP-IDF commit before and after the swap).

Verification evidence:

- `pio test -e native` — 1107/1107 pass (01:50.68); no src/* change, behaviour is
  unchanged.
- `pio run -e cardputer` — SUCCESS, flash 76.0% (2538743 B of 3342336 B).
- `pio run -e m5stick` — SUCCESS, flash 79.4% (2497507 B of 3145728 B).
- `node scripts/lint-build-zip-integrity.mjs` — 2 pinned ZIPs match SHA256.
- `node scripts/lint-docs.mjs` — 157 documents, 0 errors / 0 warnings.
- On-device: Cardputer ADV deauth run emits the strong-override log line + counter
  advance (see DoD scenario 5).

Follow-ups surfaced (not new to this slice):

- The `cardputer_adv` env target is still unmade; the ADV's I2C keyboard makes the
  pin-matrix conflict dormant on real hardware, so the current `m5stack-stamps3` env
  keeps working, but a proper ADV env is a tracked follow-up (LEDGER item, unchanged).
- The `[E][STA.cpp:530]` framework log is cosmetic; a `wifi_is_started()` guard
  around the `WiFi.disconnect()` call in the start-of-attack path would silence it,
  but that is unrelated to this slice and belongs in its own follow-up.
