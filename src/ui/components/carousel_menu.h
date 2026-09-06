/**
 * @file carousel_menu.h
 * @brief Animated carousel menu component with iconography
 */

#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <vector>
#include <string>
#include <functional>
#include "menu.h"
#include "footer_hints.h"
#include "../../hal/input/input_manager.h"
#include "../../modules/storage/settings_manager.h"

namespace adversary {

/**
 * @brief Entry in the carousel
 */
struct CarouselItem {
    std::string label;
    const uint8_t* iconData; // Pointer to embedded BMP data
    int actionId;
    bool enabled = true;
    std::string disabledReason;
};

/**
 * @brief Carousel-style menu for high-impact navigation
 */
class CarouselMenu {
public:
    using ActionCallback = std::function<void(int actionId)>;

    CarouselMenu();
    ~CarouselMenu() = default;

    void setItems(const std::vector<CarouselItem>& items);
    void setOnAction(ActionCallback callback) { callback_ = callback; }
    
    bool handleInput(char key);
    bool handleAction(InputAction action);

    void next();
    void prev();
    void select();

    bool isAnimating() const { return targetOffset_ != 0 || offset_ != 0; }

    template<typename Canvas>
    void render(Canvas& canvas);

    std::vector<CarouselItem> items_;
    int selection_;
    ActionCallback callback_;
    
    // Animation state
    float offset_;       // Smooth transition offset [-1.0, 1.0]
    float targetOffset_; // Target offset for animation
    uint32_t lastAnimTime_;
    
    // Footer Hints
    ui::FooterHints footerHints_;
};

} // namespace adversary

#ifdef ESP32
#include "../theme.h"
#include "../../config/config.h"
#include "../../utils/bitmap_remapper.h"
#include "status_bar.h"

namespace adversary {

template<typename Canvas>
void CarouselMenu::render(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    
    // Status Bar
    ui::StatusBar::render(canvas, "THE ADVERSARY");

    if (items_.empty()) return;

    // Animation progress
    uint32_t now = millis();
    float dt = (now - lastAnimTime_) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f; // Cap delta to avoid jumps
    lastAnimTime_ = now;

    // Animation progress calculation with step-limiting to prevent oscillation
    const float animSpeed = 10.0f; // Slightly faster for snappier feel
    float diff = targetOffset_ - offset_;
    float move = animSpeed * dt;

    if (abs(diff) <= move) {
        offset_ = targetOffset_;
        // Snap logic triggered when we arrive exactly at target
        if (targetOffset_ != 0) {
            if (targetOffset_ > 0.5f) selection_ = (selection_ - 1 + items_.size()) % items_.size();
            else if (targetOffset_ < -0.5f) selection_ = (selection_ + 1) % items_.size();
            offset_ = 0;
            targetOffset_ = 0;
        }
    } else {
        if (diff > 0) offset_ += move;
        else offset_ -= move;
    }

    int centerX = config::SCREEN_WIDTH / 2;
    int centerY = config::SCREEN_HEIGHT / 2; // Perfectly centered between status and footer
    
    // Render 3 items: Prev, Current, Next
    for (int i = -1; i <= 1; i++) {
        int idx = (selection_ + i + items_.size()) % items_.size();
        const auto& item = items_[idx];
        
        // Horizontal position calculation
        float pos = i + offset_;
        int x = centerX + (int)(pos * 80); // Spacing
        
        // Scale and Opacity based on distance from center
        float dist = abs(pos);
        float scale = 1.0f - (dist * 0.3f); // 1.0 to 0.7
        // uint8_t alpha = (uint8_t)(255 * (1.0f - (dist * 0.5f))); // 255 to 127
        
        // Draw icon (48x48 base)
        int size = (int)(48 * scale);
        
        if (item.iconData) {
            int drawX = x - size/2;
            int drawY = centerY - size/2;
            
            // Use disabled color if item is not enabled
            uint16_t iconColor = item.enabled ? theme::ACCENT() : theme::TEXT_DISABLED();
            utils::BitmapRemapper::drawThemeIcon(canvas, item.iconData, drawX, drawY, 48, 48, scale, iconColor);
            
            if (dist < 0.1f) {
                // Focus item: Draw label
                uint16_t textColor = item.enabled ? theme::TEXT_PRIMARY() : theme::TEXT_DISABLED();
                canvas.setTextColor(textColor);
                canvas.setTextDatum(top_center);
                canvas.setTextSize(2);
                canvas.drawString(item.label.c_str(), centerX, drawY + 50); // Slightly tighter
            }
        }
    }
    
    
    // Key hints via FooterHints component
    // We access the private member via friend or just rely on render() being member
    // Since this is member function, we can access footerHints_
    
    // Ensure footer hints are set up if empty (fallback)
    // Note: Usually handled in setItems, but good to be safe
    // But since render is const-ish regarding logic (mostly), we shouldn't modify logic here
    // Just render it.
    
    // Cast away constness if needed? No, footerHints_.render receives canvas.
    const_cast<CarouselMenu*>(this)->footerHints_.render(canvas);
}

} // namespace adversary
#endif
