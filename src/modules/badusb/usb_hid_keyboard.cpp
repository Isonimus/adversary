#include "usb_hid_keyboard.h"

#if defined(TARGET_CARDPUTER)
#include "hid_keycodes.h"
#include "usb_hid_devices.h"
#include <Arduino.h>

namespace adversary {

static UsbHidKeyboard* s_instance = nullptr;

UsbHidKeyboard& UsbHidKeyboard::getInstance() {
    if (!s_instance) s_instance = new UsbHidKeyboard();
    return *s_instance;
}

bool UsbHidKeyboard::begin() {
    UsbHidDevices::getInstance().begin();
    return true;
}

void UsbHidKeyboard::end() {
    // No USB.end() restores the JTAG-serial PHY — a hardware reset is
    // required. Nothing to do; isReady() reflects the shared registrar,
    // which the Mouse Jiggler may also be holding open.
}

bool UsbHidKeyboard::isReady() const {
    return UsbHidDevices::getInstance().isReady();
}

void UsbHidKeyboard::pressKey(uint8_t hidUsage, uint8_t modifiers) {
    if (!isReady()) return;
    KeyReport report = {0};
    report.modifiers = modifiers;
    report.keys[0]   = hidUsage;
    UsbHidDevices::getInstance().keyboard().sendReport(&report);
    delay(60);
    KeyReport release = {0};
    UsbHidDevices::getInstance().keyboard().sendReport(&release);
    delay(30);
}

void UsbHidKeyboard::releaseAll() {
    if (!isReady()) return;
    KeyReport release = {0};
    UsbHidDevices::getInstance().keyboard().sendReport(&release);
}

void UsbHidKeyboard::typeChar(char c) {
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) return;
    const hid_keycodes::HidEntry& e =
        (m_layout == Layout::ES) ? hid_keycodes::asciiToHid_ES[idx]
                                 : hid_keycodes::asciiToHid_US[idx];
    if (e.usage != 0) pressKey(e.usage, e.modifiers);
}

void UsbHidKeyboard::typeString(const char* s) {
    if (!s) return;
    while (*s) {
        typeChar(*s++);
        delay(20);
    }
}

} // namespace adversary

#endif // TARGET_CARDPUTER
