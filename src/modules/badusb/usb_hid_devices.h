#pragma once

#if defined(TARGET_CARDPUTER)
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"

namespace adversary {

// Single shared owner of the ESP32-S3 native USB HID descriptor set.
//
// TinyUSB finalizes the composite HID report-descriptor set at USB.begin();
// every USBHIDKeyboard/USBHIDMouse instance must exist (so its descriptor
// registers) before that call runs. BadUSB and the Mouse Jiggler both
// activate the same physical USB HID interface on demand, so they share
// this registrar to guarantee both descriptors are always present and
// USB.begin() only ever runs once per boot, regardless of which feature
// is opened first.
class UsbHidDevices {
public:
    static UsbHidDevices& getInstance();

    // Idempotent — safe to call from either feature. Kills the /dev/ttyACM
    // serial console; there is no way back without a hardware reset.
    void begin();
    bool isReady() const { return m_ready; }

    USBHIDKeyboard& keyboard() { return m_kbd; }
    USBHIDMouse& mouse() { return m_mouse; }

private:
    UsbHidDevices() = default;
    USBHIDKeyboard m_kbd;
    USBHIDMouse m_mouse;
    bool m_ready = false;
};

} // namespace adversary

#endif // TARGET_CARDPUTER
