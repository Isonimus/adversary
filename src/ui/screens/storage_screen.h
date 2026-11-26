/**
 * @file storage_screen.h
 * @brief Read-only SD Storage overview (ADR-0166 / slice-0167).
 *
 * Top-level carousel entry that answers "where does my data live and how much
 * is there": SD card capacity header plus one scrollable row per curated path
 * under `/adversary/`. No actions, no drill-in, no file selection — the
 * per-type management screens (Captures Browser, IR Record, SubGHz captures,
 * Saved Networks) own write semantics for their own content, per ADR-0166.
 */

#pragma once

#include <cstdint>

#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "../components/list_scroll_model.h"
#include "../components/scrollable_list.h"
#include "screen_interface.h"
#include "utils/storage_overview.h"

namespace adversary {

class StorageScreen : public IScreen {
public:
    StorageScreen();
    ~StorageScreen() override;

    // IScreen
    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }
    bool handleInput(char key) override;
    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }
    const char* getName() const override { return "Storage"; }
    ScreenId getId() const override { return ScreenId::STORAGE; }
    void init() override;
    void deinit();

private:
    // The walk is spread one curated folder per frame so the first paint
    // happens immediately (showing "Loading..."), not after the whole SD tree
    // has been scanned. A single folder with 500+ files is slow over SPI, so a
    // synchronous walk on show() looked frozen (slice-0167 field note).
    enum class Phase : uint8_t { Loading, Ready };

    void drawHeader(Canvas& canvas);
    void drawCapacityPanel(Canvas& canvas, int16_t top);
    void drawRows(Canvas& canvas, int16_t top);
    void drawNotMounted(Canvas& canvas);
    void drawLoading(Canvas& canvas);
    void stepWalk();  // scan the next pending folder into rows_[loadIndex_]

    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    bool sdMounted_;
    Phase phase_;
    bool loadingPrimed_;  // one "Loading" frame must paint before the first scan
    size_t loadIndex_;    // next curated folder to scan while Phase::Loading

    // One slot per curated path; walked one folder per frame after show().
    StorageRow rows_[CURATED_PATH_COUNT];
    uint64_t cardTotalBytes_;      // SD.cardSize(), snapshot at show() time
    uint64_t curatedTotalBytes_;   // running sum across scanned rows

    ListScrollModel scrollModel_;
    ui::FooterHints footerHints_;

    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t CAPACITY_PANEL_HEIGHT = 30;
    static constexpr int16_t LINE_HEIGHT = 12;
};

}  // namespace adversary
