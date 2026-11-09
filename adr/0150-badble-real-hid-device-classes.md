---
id: '0150'
title: "BadBLE real HID device classes: per-class report map chosen at attack start"
type: architecture
status: accepted
date: 2026-11-09
supersedes: []
superseded_by: []
---

## Context

BadBLE advertises a BLE HID-over-GATT device and injects input to a bonded host. Today it
can only ever be a **keyboard**, by construction:

- The HID Report Map characteristic (`0x2a4b`) is a single hardcoded keyboard descriptor
  (`ble_spanner.cpp`, `reportMap[]`): 8-bit modifiers, a reserved byte, and a 6-byte key
  array. There is one Input Report characteristic (`0x2a4d`) with a Report Reference of
  `[Report ID 0, Input]`.
- The only input paths are `sendKey(keycode, modifiers)` and `typeString(text)`; both emit
  the 8-byte keyboard report. There is no pointer or axis path.
- `BleSpamProvider::HID_KEYBOARD` is the only HID provider.
- The config-form "Appearance:" row cycles the GAP appearance value (keyboard `0x03C1`,
  mouse `0x03C2`, joystick `0x03C3`, gamepad `0x03C4`). That value is only the advertised
  GAP icon. The report map stays a keyboard regardless, so a host that honours the icon
  shows a mouse while the device still speaks keyboard. v0.7.1 relabelled the row from
  "Type:" to "Appearance:" to stop *claiming* it set the device class, but the underlying
  dishonesty (icon can say mouse, device is a keyboard) remained.

The gap: BadBLE cannot present as a real mouse or gamepad, so pointer-driven and
axis-driven attacks (and any host that gates on the real HID class) are impossible, and the
appearance icon can misrepresent the device.

A second force is the planned USB BadUSB module (DuckyScript USB-HID, still Phase 0, not
built). It will need the same keyboard / mouse / gamepad HID report descriptors and the
same report byte layouts over a different transport. The ASCII-to-HID keyboard table is
*already* shared between both paths (`ble_spanner.cpp` includes
`modules/badusb/hid_keycodes.h`). Mouse and gamepad are the second and third instances of
transport-agnostic HID data, which is where the rule of three says to extract.

## Decision

Make the HID **class** real, chosen at attack start, and swap the whole report map per
class. One report map, one Input Report characteristic, Report ID 0 per session, exactly as
the keyboard path works today.

**1. Per-class swap, not a composite device.** At start the selected class builds exactly
one report map and one `0x2a4d` characteristic whose Report Reference stays `[0x00, 0x01]`
(Report ID 0, Input). Reports carry no Report ID prefix, unchanged from today. We do *not*
build a composite keyboard+mouse+gamepad device with per-report IDs and three
characteristics: nothing in the BadBLE UX needs two classes live in one session (the UI is
modal: pick an attack, start it), a composite device is a "combo" whose appearance icon
cannot honestly be any single class, and the Report ID prefix would churn every existing
report. Per-class swap makes the device present *as exactly* the selected class, which is
what makes the appearance icon honest.

**2. Three real classes: Keyboard, Mouse, Gamepad.** The GAP appearance is derived from the
class, not selected separately:

| Class | Appearance | Input report (Report ID 0) |
|-------|-----------|----------------------------|
| Keyboard | `0x03C1` | `[modifiers, reserved, k1..k6]` (8 bytes), unchanged |
| Mouse | `0x03C2` | `[buttons, dx, dy, wheel]` (4 bytes): buttons = 3 bits L/R/M + 5 padding; dx/dy/wheel signed int8, relative |
| Gamepad | `0x03C4` | `[buttons, x, y]` (3 bytes): buttons = 8 bits; x/y signed int8 axes |

The gamepad report starts minimal (two axes + eight buttons); more axes are added only when
a target needs them, not speculatively. "Joystick" (`0x03C3`) is folded into Gamepad: it is
the same axes-plus-buttons HID model, so a separate joystick map would be a near-duplicate.
A distinct joystick usage-page descriptor is a trivial future add if a real target ever
requires the joystick usage specifically (ledgered).

**3. The cosmetic appearance selector becomes the real class selector.** The config-form row
stops being a standalone GAP-icon cycler and instead picks the HID class; the class drives
both the report map and the appearance. This collapses the honesty gap the v0.7.1 relabel
could only label.

**4. Report descriptors and report byte-packing become shared, pure, native-testable
functions**, placed next to the existing shared keyboard table (`modules/badusb/`, the
home `hid_keycodes.h` already establishes). The descriptor bytes for each class and small
pure builders (for example `buildMouseReport(dx, dy, buttons, wheel)` and
`buildGamepadReport(x, y, buttons)`) live there. BadBLE wraps a built report in a GATT
notify; the USB BadUSB module, when built, wraps the same bytes in its HID endpoint. The
keyboard table is already shared, so mouse and gamepad join it rather than being duplicated
when USB lands.

**5. New class-guarded send API.** Add `sendMouseMove(int8_t dx, int8_t dy, uint8_t
buttons, int8_t wheel)` and `sendGamepad(int8_t x, int8_t y, uint8_t buttons)` alongside
`sendKey`/`typeString`. Each send asserts the active class matches the report map and fails
loud (logs and refuses) if called against a mismatched class, since a mouse report sent
under a keyboard map is a silent protocol error on the host.

**6. Name the provider honestly.** Rename `BleSpamProvider::HID_KEYBOARD` to `HID` (it gates
"HID attack vs spam advertisement", not the class) and add `enum class BleHidClass {
KEYBOARD, MOUSE, GAMEPAD }` with a `BleHidClass hidClass` field in the BadBLE config. The
service builder gates on the provider being `HID` and switches the report map on `hidClass`.

Consistency with ADR-0134 (fresh random identity per session, reconnect-storm guard,
Just-Works-bondable security) is preserved unchanged: this decision only swaps the report
map and adds pointer/axis report paths behind the same identity and security model.

## Consequences

- The appearance icon always matches the real device class, because one value drives both.
  The honesty gap closes.
- Mouse and gamepad attacks become possible (pointer move, buttons, wheel, axes).
- The report byte-packing is extracted as pure functions, so each class gets a
  fail-before/pass-after native test asserting the exact report bytes: observable behaviour,
  not internals. This is the first place these layouts are testable at all.
- One class per session is a deliberate limitation, accepted: the BadBLE UX is modal and no
  current attack needs two classes at once. Revisiting this means a composite device, a
  separate decision if a real need appears.
- The BadUSB reuse seam is established (shared descriptors + packers) but not yet consumed;
  the USB module is still Phase 0. This decision only forbids a second hardcoded copy when
  USB lands.
- UI work follows: the appearance cycler becomes a HID-class cycler, and mouse/gamepad need
  their own action sub-screens (the existing Windows/Linux/macOS script screens are
  keyboard-only). That build is out of scope here and tracked in the ledger.
- Hosts that previously saw a keyboard with a mouse icon now see a real mouse when Mouse is
  selected; this is the intended behaviour change, not a regression.
