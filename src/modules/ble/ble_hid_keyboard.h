#pragma once
#include "modules/badusb/i_hid_keyboard.h"

namespace adversary {

// IHidKeyboard adapter over BLESpanner for DuckyScript injection via BLE HID.
// begin()/end() are no-ops — BLE lifecycle is owned by BLESpanner.
// Layout is kept in sync with BLESpanner::setLayout() so the existing BadBLE
// quick-action typeString() calls also honour the current layout.
class BleHidKeyboard : public IHidKeyboard {
public:
    static BleHidKeyboard& getInstance();

    bool begin() override;
    void end() override;
    bool isReady() const override;

    void setLayout(Layout layout) override;
    Layout getLayout() const override;

    void pressKey(uint8_t hidUsage, uint8_t modifiers) override;
    void releaseAll() override;

    void typeChar(char c) override;
    void typeString(const char* s) override;

private:
    BleHidKeyboard() = default;
};

} // namespace adversary
