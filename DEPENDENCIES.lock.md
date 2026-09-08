# Dependency lock — resolved tree

PlatformIO has **no native lockfile**. `platformio.ini` pins every dependency this
project *declares* to an exact version (no `^` ranges), but a library's own manifest can
still declare its children with `*`, which drifts on a fresh resolve. To close that gap,
the transitive deps of the M5 board libs are pinned explicitly in `platformio.ini` too,
and this file records the **complete** resolved tree — direct, transitive, platform, and
toolchain — as a diffable snapshot.

Purpose: a clone must build the same bytes we tested. If a rebuild resolves anything not
listed here, that is drift — investigate before trusting the build.

- **Verify:** `pio pkg list -e cardputer` (and `-e m5stick`, `-e native`) must match the
  trees below.
- **Bump:** change the pin in `platformio.ini`, re-resolve, rebuild all three envs, run
  `pio test -e native`, then update this snapshot **in the same commit**.
- **Platform / arduino-libs** are pinned by exact release-ZIP URL in `platformio.ini`
  (`[env:cardputer]` / `[env:m5stick]`); the resolved versions are recorded below.

Last verified: 2026-09-08 — native 706/706, cardputer + m5stick builds SUCCESS.

---

## Declared pins (`platformio.ini`)

| Library | Pin | Env | Notes |
|---|---|---|---|
| bblanchon/ArduinoJson | 7.4.3 | all | was `^7.2.1`; native previously skewed to 7.4.2 |
| h2zero/NimBLE-Arduino | 1.4.3 | embedded | was `^1.4.1` |
| kkloesener/MFRC522_I2C | 1.0.0 | embedded | was `^1.0` (only 1.0.0 published) |
| crankyoldgit/IRremoteESP8266 | 2.9.0 | embedded | was `^2.8.6` |
| m5stack/M5GFX | 0.2.28 | embedded | transitive of both board libs; was `*` |
| m5stack/M5Unified | 0.2.21 | embedded | transitive of both board libs; was `*` |
| m5stack/M5Cardputer | 1.1.1 | cardputer | was `^1.0.0` |
| arduino-irremote/IRremote | 4.7.1 | cardputer | transitive of M5Cardputer; was `*` |
| m5stack/M5StickCPlus2 | 1.0.2 | m5stick | was `^1.0.0` |
| m5stack/M5Family | 0.1.3 | m5stick | transitive of M5StickCPlus2; was `*` |
| dfrobot/DFRobot_GP8XXX | 1.1.0 | m5stick | transitive of M5StickCPlus2; was `*` |
| fastled/FastLED | 3.10.3 | cardputer, m5stick | was `^3.6.0` |
| throwtheswitch/Unity | 2.6.0 | native | was `^2.5.2` |

`arduino-irremote/IRremote` (the Arduino-IRremote 4.x lib pulled by M5Cardputer) is a
*different* library from `crankyoldgit/IRremoteESP8266` (which the firmware uses directly).
The owner alias matters: the registry also exposes `z3t0/IRremote@4.7.1`; the installed
package's repository is `github.com/Arduino-IRremote/Arduino-IRremote`, i.e. the
`arduino-irremote` owner — pin that one so the resolver never double-installs.

---

## Resolved library tree

### `cardputer`
```
ArduinoJson         @ 7.4.3
FastLED             @ 3.10.3
IRremote            @ 4.7.1        (arduino-irremote/IRremote)
IRremoteESP8266     @ 2.9.0
M5Cardputer        @ 1.1.1
├── IRremote        @ 4.7.1
├── M5GFX           @ 0.2.28
└── M5Unified       @ 0.2.21
MFRC522_I2C         @ 1.0.0
NimBLE-Arduino      @ 1.4.3
```

### `m5stick`
```
ArduinoJson         @ 7.4.3
DFRobot_GP8XXX      @ 1.1.0
FastLED             @ 3.10.3
IRremoteESP8266     @ 2.9.0
M5Family            @ 0.1.3
M5GFX               @ 0.2.28
M5StickCPlus2      @ 1.0.2
└── M5Unified       @ 0.2.21
MFRC522_I2C         @ 1.0.0
NimBLE-Arduino      @ 1.4.3
```

### `native`
```
ArduinoJson         @ 7.4.3
Unity               @ 2.6.0
```

---

## Platform & toolchain (embedded envs)

Pinned by exact release-ZIP URL in `platformio.ini`; resolved versions:

```
platform  espressif32                       @ 55.3.34
          framework-arduinoespressif32      @ 3.3.4
          framework-arduinoespressif32-libs @ 5.5.0+sha.8410210c9a   (bmorcelli patched libs)
tools     tool-esptoolpy                    @ 5.1.0
          tool-dfuutil-arduino              @ 1.11.0
          tool-mkfatfs                      @ 2.0.1
          tool-mklittlefs                   @ 3.2.0
          tool-mklittlefs4                  @ 4.0.2
          tool-mkspiffs                     @ 2.230.0
          tool-esp-rom-elfs                 @ 2024.10.11
          toolchain-xtensa-esp-elf          @ 14.2.0+20250730
```
