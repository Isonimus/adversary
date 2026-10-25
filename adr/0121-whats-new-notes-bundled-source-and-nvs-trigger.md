---
id: '0121'
title: "On-device what's-new notes: bundled source, NVS first-boot-after-update trigger"
type: architecture
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Context

Alpha users flash The Adversary over OTA or M5Burner and never open the GitHub
`CHANGELOG.md`, so shipped features go unnoticed (ledger item, C-level assessment
2026-10-15). The device should surface, on its own screen, a short "what changed" note for
the version now running.

Two facts in the code shape the mechanism, both measured, not assumed:

- **`VERSION` is the single source of truth** (`src/config/config.h`, currently
  `"0.5.0-alpha"`), bumped once per release (see the CLAUDE.md release checklist). Any
  "has the running version changed?" test compares against this one constant.
- **The only persistence we have today is SD-backed** (`SystemSettings` -> JSON on the SD
  card). A `grep` for `Preferences`/`nvs_`/`NVS` across `src/` returns nothing: there is no
  flash-backed key-value store in the firmware. This matters because the target audience is
  precisely the operator with **no SD card** (OTA/M5Burner, bench use). A "last seen
  version" kept in SD settings would, for that operator, reset to its default on every boot
  (settings never persist without a card), so an auto-show keyed on it would fire on *every*
  boot, not once per update: it would mis-fire for exactly the users the feature exists for.

The firmware already has the right screen shape to reuse: `AboutScreen` is a scrollable
block of static lines (`MANIFESTO_LINES[]` + a scroll offset, ESC to menu). The what's-new
screen is the same idiom with different content, not a new UI mechanism.

## Decision

**1. Notes source: a bundled, hand-curated header, not a generated artifact.**
`src/config/whats_new.h` holds the running version's notes as a `const char* const`
line array (`WHATS_NEW_LINES[]` + `WHATS_NEW_LINE_COUNT`), operator-pitched and short
(a screenful), covering **only the running version** (the delta since the operator's
last-seen version is what matters). It is not generated from `CHANGELOG.md`: generation
would couple the build to changelog markup for no gain, and the on-device copy is
deliberately terser and more operator-facing than the developer changelog. Curating this
header becomes one step in the release checklist (added to CLAUDE.md), alongside the
`CHANGELOG.md` section it summarizes.

**2. Trigger marker: NVS via the Arduino `Preferences` library, not SD settings.**
A `lastSeenVersion` string is persisted in ESP32 NVS (namespace `"adversary"`, key
`"lastSeenVer"`). NVS is flash-backed and SD-independent, so it survives reboot for the
cardless operator, which is the whole requirement from the Context. `Preferences` is part
of the Arduino-ESP32 core (no new `lib_deps`, no partition change: the default table already
carries an `nvs` partition). This is a deliberately new, second persistence store; its
**boundary** is recorded so it does not sprawl: NVS holds only *device-level markers that
must survive without an SD card* (today: the what's-new marker). Operator settings stay in
SD JSON. A reader/writer pair lives behind one small helper, not scattered `Preferences`
opens.

**3. Both triggers (per operator decision 2026-10-24).**
- **Auto-show once per update.** At idle, right after the menu is initialized, compare the
  NVS `lastSeenVersion` to `VERSION`. If they differ, route to the What's New screen and
  write `VERSION` back to NVS so it shows exactly once until the next version bump. A
  first-ever boot (empty key) counts as "differs" and shows, because a fresh flash is
  exactly when the operator wants the notes. The write happens when the screen is triggered,
  not on dismiss: a power-cut before reading simply re-shows next boot (idempotent, no lost
  state).
- **On demand.** A Settings action row "What's New" opens the same screen any time, so the
  notes are re-readable after the one-shot auto-show. It routes via the existing
  settings-callback pattern (mirrors "Saved Networks").

**4. A dedicated `ScreenId::WHATS_NEW` screen** mirroring the `AboutScreen` scrollable-text
pattern (static line array + scroll offset + N/total indicator, ESC returns to menu). No
new scroll/render mechanism is invented.

## Consequences

- **Every release gains one obligation:** update `whats_new.h` for the new `VERSION`. If it
  is forgotten, the screen shows the previous version's text under the new version heading.
  That is caught by the mandatory on-device step of the release checklist (the auto-show
  fires the first time the release image boots). No runtime "fail loud" on content staleness
  is added: a string-equality guard between `whats_new.h` and `VERSION` would be compile-time
  awkward (string literals) and runtime noise for a purely editorial slip. The checklist plus
  the forced on-boot surfacing is the control.
- **A second persistence store exists.** The risk is NVS turning into a junk drawer that
  should have been a setting. The recorded boundary (device-level, must-survive-without-SD
  markers only) keeps settings in SD JSON where they are exported and inspectable, and keeps
  NVS to the narrow case SD cannot serve.
- **Cost is negligible and will be measured, not estimated:** the notes add a few hundred
  bytes of flash; NVS uses the existing partition. The slice records the actual build delta.
- Reusing the `AboutScreen` idiom (rather than generalizing it now) is deliberate: two
  scrollable-static-text screens is the second instance, not the third, so the rule of three
  says copy the shape, not extract a shared component yet. If a third such screen appears,
  that extraction is the follow-up.
