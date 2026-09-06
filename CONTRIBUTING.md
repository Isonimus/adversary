# Contributing to The Adversary

Thanks for your interest. A few things make a contribution land smoothly.

## Before anything: authorized use only

The Adversary is red-team tooling. By contributing you accept that it is for
**authorized testing, education, and research** only — the terms in the
[DISCLAIMER](DISCLAIMER.md). Contributions whose only purpose is to harm third parties,
evade the law, or target people without consent will be declined.

## Development setup

Install [PlatformIO](https://platformio.org/) (the VS Code extension, or
`pip install platformio`), clone the repo, and use the environments defined in
`platformio.ini`:

```bash
pio test -e native      # host unit suite (Unity)
pio run  -e cardputer   # primary firmware — M5Stack Cardputer / ADV (ESP32-S3)
pio run  -e m5stick     # secondary target — M5StickC Plus2 (ESP32-PICO)
```

CI runs all three, plus CodeQL and cppcheck, on every push and pull request — a PR must
be green to merge.

## Code standards

The full engineering bar lives in [CLAUDE.md](CLAUDE.md). It is written for AI-assisted
work, but the standards apply to everyone: C++17, HAL-first architecture, event-driven
modules, fail-loud error handling, no magic values, and a regression test for every piece
of logic. Please read it before a non-trivial change.

- Match the surrounding style (see the naming table in CLAUDE.md).
- Add or extend Unity tests under `test/` for any logic you change.
- Keep hardware-specific code behind the HAL and the `pins.h` target guards.

## Commits and pull requests

- **Conventional commits**: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`,
  with an optional scope (`fix(sd): …`). One logical change per commit.
- Explain **why** in the body, not just what.
- Fork → branch (`feature/…` or `fix/…`) → open a PR against `main`.
- Fill in the PR template, and make sure `pio test -e native` and both firmware builds
  pass locally first.

## A note on `adr/` and `slices/`

Those directories are the project's **decision records** (an ADR/slice workflow the
maintainer uses). You are welcome to read them for context, but you do **not** need to
write them to contribute — just describe your change in the PR and link the issue it
addresses.

## Reporting bugs and requesting features

Use the issue templates. For anything security-sensitive in the firmware itself, follow
[SECURITY.md](SECURITY.md) instead of opening a public issue.
