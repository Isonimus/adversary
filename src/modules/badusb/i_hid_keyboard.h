#pragma once
#include <stdint.h>

namespace adversary {

class IHidKeyboard {
public:
    enum class Layout { US, ES };

    virtual ~IHidKeyboard() = default;

    virtual bool begin() = 0;
    virtual void end() = 0;
    virtual bool isReady() const = 0;

    virtual void setLayout(Layout layout) = 0;
    virtual Layout getLayout() const = 0;

    // Press a key by HID usage code + modifier bitmask, then release.
    virtual void pressKey(uint8_t hidUsage, uint8_t modifiers) = 0;
    virtual void releaseAll() = 0;

    // Type a single character using the active layout table.
    virtual void typeChar(char c) = 0;
    virtual void typeString(const char* s) = 0;
};

} // namespace adversary
