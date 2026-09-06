#include "usb_hid_devices.h"

#if defined(TARGET_CARDPUTER)

namespace adversary {

static UsbHidDevices* s_instance = nullptr;

UsbHidDevices& UsbHidDevices::getInstance() {
    if (!s_instance) s_instance = new UsbHidDevices();
    return *s_instance;
}

void UsbHidDevices::begin() {
    if (m_ready) return;
    m_kbd.begin();
    m_mouse.begin();
    USB.begin();
    m_ready = true;
}

} // namespace adversary

#endif // TARGET_CARDPUTER
