/**
 * @file about_screen.h
 * @brief About screen with The Adversary Manifesto
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "screen_interface.h"

namespace adversary {

/**
 * @brief About screen displaying manifesto and credits
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class AboutScreen : public IScreen {
public:
    AboutScreen();
    ~AboutScreen() override;
    
    // =========================================================================
    // IScreen Interface
    // =========================================================================
    
    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }
    
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }
    
    bool handleInput(char key) override;
    
    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }
    
    const char* getName() const override { return "About"; }
    ScreenId getId() const override { return ScreenId::ABOUT; }

    void init() override;
    void deinit();
    
    static const char* MANIFESTO_LINES[];
    static int getManifestoLineCount();

private:
    void drawHeader(Canvas& canvas);
    void drawContent(Canvas& canvas);
    
    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    int scrollOffset_;
    
    // UI Components
    ui::FooterHints footerHints_;
    
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t LINE_HEIGHT = 10;
};

} // namespace adversary
