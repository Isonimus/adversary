# Security Policy

## What counts as a security issue here

The Adversary is **offensive security tooling** — red-team wireless pentesting firmware.
Its WiFi, BLE, RF, and HID attack capabilities are the intended function, **not**
vulnerabilities. All use is governed by the [DISCLAIMER](DISCLAIMER.md): authorized
testing, education, and research only.

A reportable security issue is a defect **in the firmware itself** that puts its
*operator* or their data at risk — for example:

- The on-device web dashboard leaking captures, or bypassing its authentication or
  path-traversal protections.
- Cloud sync (WPA-SEC / pwncrack / WiGLE) exposing an API key — e.g. a TLS-validation
  regression that would let a key be MITM'd.
- Any remote-code-execution, credential-leak, or data-loss bug in the firmware.

Please do **not** report the existence of the attack features themselves, and do not ask
for help attacking a network you are not authorized to test.

## Supported versions

The project is in **alpha**. Only the latest `main` is supported; fixes land there.

| Version            | Supported |
| ------------------ | --------- |
| latest `main`      | ✅        |
| older commits/tags | ❌        |

## Reporting a vulnerability

Report privately — not in a public issue:

1. **Preferred:** GitHub private vulnerability reporting — this repository's
   **Security → Advisories → Report a vulnerability** tab.
2. Otherwise, contact the maintainer [@Isonimus](https://github.com/Isonimus).

Please include the affected version or commit, the affected device (Cardputer ADV /
original Cardputer / M5StickC Plus2), reproduction steps, and impact. There is no
bug-bounty program — this is a research/hobby project — but reports are welcome and will
be credited if you would like.
