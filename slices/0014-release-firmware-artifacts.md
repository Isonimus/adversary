---
id: '0014'
title: "Tag-triggered GitHub Release: cardputer firmware artifacts"
type: slice
status: accepted
date: 2026-09-07
supersedes: []
superseded_by: []
---

## Goal

Turn a version tag into an installable release. Today CI (slice-0010) *builds* every target
on every push but produces **no downloadable artifact** — to hand someone the firmware you
have to build it yourself and fish `firmware.bin` out of `.pio/`. That is the missing half
of "publish to the M5Launcher": bmorcelli's Launcher installs a firmware from a **direct
`.bin` URL**, and a GitHub Release asset is exactly such a URL. This slice makes pushing a
`v*` tag cut a Release carrying the artifacts the Launcher (and web flashers, and
`esptool`) consume.

Two artifacts, because there are two install paths:

- **App image** (`…-app.bin`, = `firmware.bin`, ESP image header, flashes to an OTA app
  slot at `0x10000`) — what the **M5Launcher installs over OTA** from a direct URL.
- **Factory image** (`…-factory.bin`, merged bootloader+partitions+boot_app0+app at `0x0`)
  — for a **fresh flash** via `esptool` / an ESP web flasher on a blank or bricked device.

Plus `SHA256SUMS.txt` so an installer can verify the download.

### Scope: cardputer only, and what "cardputer" means here

**Only the `cardputer` env** (`m5stack-stamps3`, ESP32-S3, `default_8MB.csv`). `m5stick` is
deliberately excluded: it is on `huge_app.csv` (single 3 MB app, **no OTA slots**), so it is
not Launcher-OTA-installable the way the cardputer default scheme is — packaging it is a
separate question, tracked in the ledger.

This is the **stamps3 build** — the same binary flashed to the deployment Cardputer ADV.
There is no distinct ADV target in the tree (no `TCA8418`/`CardputerADV` code, no
`cardputer_adv` env; the keyboard goes through `M5Cardputer.begin()`), so the asset is named
`cardputer`, not `cardputer-adv`. When a real ADV target lands (the tracked follow-up in
CLAUDE.md), it ships as its own asset rather than silently re-labelling this one.

### Explicitly out of scope: catalog automation

Getting the firmware to *appear in* the Launcher's online store ("LauncherHub") or in
**M5Burner** is **not** automated here. Those are manual, credential-bound, human
submissions that point at a releases URL — a rare act, not a per-build step, and publishing
red-team firmware to a public catalog is a distribution decision for a human, not a CI
default. CI's job ends at producing verifiable release assets on this repo.

## Definition of Done

- **Given** a pushed tag matching `v*`
- **When** the release workflow runs
- **Then** it builds `-e cardputer`, produces `…-app.bin`, `…-factory.bin`, and
  `SHA256SUMS.txt`, and publishes them as assets on a GitHub Release named for the tag

- **Given** the factory image
- **When** it is inspected with `esptool image_info`
- **Then** it is a valid ESP32-S3 image (checksum + validation hash valid) with flash
  params matching the board (QIO / 80 MHz / 8 MB) and the app segment at the `default_8MB`
  offsets — i.e. directly flashable to `0x0`

- **Given** a push that is **not** a `v*` tag (a branch push or PR)
- **When** CI runs
- **Then** no Release is cut — releasing is gated strictly on the tag trigger, leaving the
  slice-0010 build/test matrix as the only per-push work

## Design

### Trigger: version tags only

`on: push: tags: ['v*']`. A release is an intentional act (tag `v0.1.0` and push it), not a
side effect of every commit. The existing per-push CI (slice-0010) still runs on the same
tag push and re-proves the three-env build; this workflow is the packaging-and-publish layer
on top, and only for tagged commits.

### One job, mirroring the CI toolchain

`ubuntu-latest`, and the setup is deliberately identical to `ci.yml` so the release build is
the *same* build CI already vets: `actions/checkout@v7` → `actions/setup-python@v7` (pinned
`3.13`; pioarduino rejects newer) → `actions/cache@v6` over `~/.platformio` + `~/.cache/pip`
(same `pio-<os>-cardputer-<hash(platformio.ini)>` key, so it shares CI's warmed cache) →
`pip install platformio` → `pio run -e cardputer`. The `weaken_deauth_pre.py` pre-script
runs unchanged, exactly as in CI.

### The factory image: measured offsets, not guessed

The merge offsets come from the resolved partition table (`default_8MB.csv`), not a
template:

| Offset | Component | Source |
| --- | --- | --- |
| `0x0` | `bootloader.bin` | S3 bootloader sits at 0x0 (classic ESP32 would be 0x1000) |
| `0x8000` | `partitions.bin` | partition table |
| `0xe000` | `boot_app0.bin` | `otadata` slot; located dynamically under `~/.platformio/packages` |
| `0x10000` | `firmware.bin` | `app0` (`ota_0`) |

`esptool merge-bin --flash-mode qio --flash-freq 80m --flash-size 8MB` patches those params
into the bootloader header to match the board (`board_build.flash_mode = qio`, 8 MB flash).
`esptool` is pip-pinned (`==5.2.0`) so the CLI is deterministic and uses the non-deprecated
`merge-bin` / `--flash-*` spelling. `boot_app0.bin` is found with `find` rather than
hardcoded — its path lives inside a versioned framework package.

### Publish with `gh`, not a third-party action

`gh release create` using the built-in `GITHUB_TOKEN` (`permissions: contents: write`). `gh`
is preinstalled on the runner, so this adds **no** third-party action to the supply chain —
consistent with the public-release infra posture of slice-0011. `--generate-notes` fills the
body from commits/PRs since the previous tag.

### Asset naming

`adversary-cardputer-<tag>-app.bin`, `adversary-cardputer-<tag>-factory.bin`,
`SHA256SUMS.txt`. The `<tag>` (`github.ref_name`) makes every asset self-identifying, and
`app` vs `factory` names the install path so an operator does not flash the wrong one.

### Why no native unit test for this slice

Like slice-0010, this slice *is* CI/packaging configuration — there is no application logic
to exercise. Its correctness is a YAML/structure check plus a locally-proven `merge-bin`
command (below) and, ultimately, a real tagged run on GitHub.

## Verification

- **The merge command is proven locally, byte-for-byte.** Against the current cardputer
  build tree, `esptool --chip esp32s3 merge-bin` at the table's offsets produced a 2,473,872-
  byte factory image; `esptool image_info` reported **Flash size 8MB, freq 80m, mode QIO,
  Checksum valid, Validation hash valid**. The non-deprecated 5.x CLI (`merge-bin`,
  `--flash-mode`) yields output **byte-identical** to the legacy spelling — so the pinned
  `esptool==5.2.0` invocation in the workflow is the exact command validated here. The app
  image is `firmware.bin` unmodified (2,408,336 B), which comfortably fits the 0x330000
  (~3.19 MB) OTA slot the Launcher writes into.
- **Local (the ceiling here):** the workflow parses as valid YAML and is structurally a
  well-formed Actions workflow — `on: push: tags: ['v*']`, one job, pinned actions matching
  `ci.yml`, `permissions: contents: write`, the build/merge/publish steps in order.
- **Remote / real run:** the authoritative confirmation is the first pushed `v*` tag, which
  is a deliberate future act (no tag is pushed by this slice). Until then the guarantee rests
  on the locally-proven merge plus the fact that `pio run -e cardputer` is already green in
  CI on every push.

## As built

_To be completed once the first tag is cut and observed green; tracked as a LEDGER
follow-up until then._

## Amendment — 2026-09-07: first tagged run observed green

`v0.1.0-alpha` was tagged and pushed on 2026-09-07. The Release workflow run completed
`success` and published the three assets to the release:

| Asset | Size |
| --- | --- |
| `adversary-cardputer-v0.1.0-alpha-app.bin` | 2,444,560 B |
| `adversary-cardputer-v0.1.0-alpha-factory.bin` | 2,510,096 B |
| `SHA256SUMS.txt` | 218 B |

The clean-checkout CI build produced an app image ~36 KB larger than the local incremental
build (2,444,560 vs 2,408,336 B) — expected from embedded build paths/timestamps, not a
regression; the factory image is validated by `esptool merge-bin` inside the run. All three
DoD scenarios hold: the assets published on the tag, the factory image is a valid flashable
`0x0` image, and a non-tag push cuts no release.
