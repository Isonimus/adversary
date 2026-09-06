#include "ble_hid_keyboard.h"
#include "modules/ble/ble_spanner.h"
#include "modules/badusb/hid_keycodes.h"
#include <Arduino.h>

namespace adversary {

BleHidKeyboard& BleHidKeyboard::getInstance() {
    static BleHidKeyboard instance;
    return instance;
}

bool BleHidKeyboard::begin() {
    return isReady();
}

void BleHidKeyboard::end() {
    // BLE lifecycle owned by BLESpanner — nothing to do here.
}

bool BleHidKeyboard::isReady() const {
    return BLESpanner::getInstance().isConnected();
}

void BleHidKeyboard::setLayout(Layout layout) {
    BLESpanner::getInstance().setLayout(
        (layout == Layout::ES) ? KeyboardLayout::ES : KeyboardLayout::US);
}

IHidKeyboard::Layout BleHidKeyboard::getLayout() const {
    return (BLESpanner::getInstance().getLayout() == KeyboardLayout::ES)
        ? Layout::ES : Layout::US;
}

void BleHidKeyboard::pressKey(uint8_t hidUsage, uint8_t modifiers) {
    // BLESpanner::sendKey() already includes press + release + vTaskDelay(30ms total).
    BLESpanner::getInstance().sendKey(hidUsage, modifiers);
}

void BleHidKeyboard::releaseAll() {
    // BLESpanner::sendKey() unconditionally sends a zero release report before
    // returning, so there are no stuck keys to clear.
}

void BleHidKeyboard::typeChar(char c) {
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) return;
    KeyboardLayout bleLayout = BLESpanner::getInstance().getLayout();
    const hid_keycodes::HidEntry& e =
        (bleLayout == KeyboardLayout::ES) ? hid_keycodes::asciiToHid_ES[idx]
                                          : hid_keycodes::asciiToHid_US[idx];
    if (e.usage != 0) pressKey(e.usage, e.modifiers);
}

void BleHidKeyboard::typeString(const char* s) {
    if (!s) return;
    while (*s) typeChar(*s++);
}

} // namespace adversary
