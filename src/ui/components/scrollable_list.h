/**
 * @file scrollable_list.h
 * @brief Device-side renderer for a scrolling selection list (slice-0062).
 *
 * The thin display half of the shared list component. It owns no state: the screen keeps the
 * pure ListScrollModel (so handleInput can drive it), and this renderer, each frame,
 *   1. derives the viewport height from the live layout rect — the single source of truth for
 *      how many rows fit, which the model's scroll window then honours;
 *   2. paints the selected row's band itself (BG_SELECTED when focused, BG_SECONDARY when the
 *      screen's footer holds focus), so the selection highlight is identical on every list;
 *   3. calls the screen's per-row draw callback for each visible item (the callback draws row
 *      content only — columns, text, badges — never its own band);
 *   4. draws the one unified scrollbar thumb when the list is scrollable.
 *
 * This replaces four divergent hand-rolled scrollbar idioms (thin bar, proportional thumb,
 * up/down triangles, "N/total" text) with a single proportional thumb. render() is a template
 * on the canvas type and is compiled on device only, mirroring Menu::render / StatusBar.
 */

#pragma once

#include <cstdint>
#include "list_scroll_model.h"

namespace adversary {

class ScrollableList {
public:
    /// Scrollbar thumb width in pixels. The one unified value; thumb *height* geometry lives
    /// in ListScrollModel (it is pure math and unit-tested), the width is purely cosmetic.
    static constexpr int16_t SCROLLBAR_WIDTH_PX = 4;

    /**
     * @brief Render the visible rows and the scrollbar for a list.
     * @param model      the screen's persistent selection/scroll state (item count kept in sync
     *                   by the screen); its viewport is set here from the rect.
     * @param x,y,w,h    the list content rect in canvas pixels.
     * @param rowHeight  pixel height of one row; visibleRows = h / rowHeight.
     * @param drawRow    callback drawing one row's content: (canvas, itemIndex, rowTopY,
     *                   isSelected). The band under the selected row is drawn here, not in the
     *                   callback; the callback must not fill its own selection band.
     * @param focused    true when the list itself has input focus (selected band = BG_SELECTED);
     *                   false when the screen's footer holds focus (band dims to BG_SECONDARY).
     *                   Screens without a focusable footer leave the default.
     */
    template<typename Canvas, typename DrawRow>
    static void render(Canvas& canvas, ListScrollModel& model,
                       int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t rowHeight, DrawRow drawRow, bool focused = true);
};

} // namespace adversary

// Template implementation is device-only (it touches the canvas and the theme), matching the
// other native-compiled components whose render() is ESP32-guarded (see menu.h).
#ifdef ESP32
#include "../theme.h"

namespace adversary {

template<typename Canvas, typename DrawRow>
void ScrollableList::render(Canvas& canvas, ListScrollModel& model,
                            int16_t x, int16_t y, int16_t w, int16_t h,
                            int16_t rowHeight, DrawRow drawRow, bool focused) {
    const int visibleRows = rowHeight > 0 ? h / rowHeight : 0;
    model.setViewport(visibleRows);

    // The selection band is drawn here, once, for every list: BG_SELECTED when the list has
    // focus, dimmed to BG_SECONDARY when the screen's footer does. The callback draws content
    // on top and never paints its own band (slice-0063). Scroll-only (read-only log) lists have
    // no selection, so no band is ever painted and every row reports isSelected=false (ADR-0133).
    const bool scrollOnly = model.isScrollOnly();
    const uint16_t bandColor = focused ? theme::BG_SELECTED() : theme::BG_SECONDARY();
    const int offset = model.scrollOffset();
    const int selected = model.selectedIndex();
    for (int row = 0; row < visibleRows && (offset + row) < model.itemCount(); ++row) {
        const int itemIndex = offset + row;
        const int16_t rowTopY = y + static_cast<int16_t>(row * rowHeight);
        const bool isSelected = !scrollOnly && (itemIndex == selected);
        if (isSelected) {
            canvas.fillRect(x, rowTopY, w, rowHeight, bandColor);
        }
        drawRow(canvas, itemIndex, rowTopY, isSelected);
    }

    if (model.isScrollable()) {
        const int16_t thumbHeight = static_cast<int16_t>(model.thumbHeightPx(h));
        const int16_t thumbTop = y + static_cast<int16_t>(model.thumbTopPx(h));
        canvas.fillRect(x + w - SCROLLBAR_WIDTH_PX, thumbTop,
                        SCROLLBAR_WIDTH_PX, thumbHeight, theme::ACCENT());
    }
}

} // namespace adversary
#endif // ESP32
