#pragma once
#include "i_hid_keyboard.h"

#if defined(TARGET_CARDPUTER)

namespace adversary {

// USB HID keyboard implementation for ESP32-S3 (Cardputer).
//
// begin() routes through UsbHidDevices, the shared registrar for the
// on-demand USB HID interface (see usb_hid_devices.h) — this switches the
// shared USB PHY from the hardware USB-Serial-JTAG peripheral (MODE=1) to
// the TinyUSB OTG stack. The /dev/ttyACM serial console is lost until the
// next hardware reset. This is intentional and expected for the
// HID-on-demand path.
class UsbHidKeyboard : public IHidKeyboard {
public:
    static UsbHidKeyboard& getInstance();

    // Brings up USB HID (kills serial console). Returns immediately; caller must
    // allow ~1.5 s before injecting keystrokes for the host to enumerate the device.
    bool begin() override;
    void end() override;
    bool isReady() const override;

    void setLayout(Layout layout) override { m_layout = layout; }
    Layout getLayout() const override { return m_layout; }

    void pressKey(uint8_t hidUsage, uint8_t modifiers) override;
    void releaseAll() override;

    void typeChar(char c) override;
    void typeString(const char* s) override;

private:
    UsbHidKeyboard() = default;
    Layout m_layout = Layout::US;
};

} // namespace adversary

#endif // TARGET_CARDPUTER
