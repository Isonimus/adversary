/**
 * @file config_row.h
 * @brief Shared chrome-only config-form row: label left, value right (ADR-0096).
 *
 * The one house look for the hand-rolled config/setup forms' uniform value rows
 * (evil_twin, karma, ble_bad_ble, handshake Manual). It draws a label at a fixed left x in
 * theme::TEXT_PRIMARY and its value string right-aligned in dim theme::TEXT_SECONDARY, taken
 * wholesale from settings_screen's canonical ~50-row list. Content only: the value STRING
 * (an ON/OFF toggle, a "60s" range, a "Keyboard" switch) is formatted by the call site,
 * because those are formatters of one row shape, not distinct shapes (ADR-0096).
 *
 * Selection is NOT this component's job: it takes no `selected` flag and draws no band. The
 * selection band is painted by the caller (the shared ScrollableList before its row
 * callback, or the screen's own fillRect for handshake, which stays off ScrollableList per
 * ADR-0094); drawing a band here would double it. Header-only like ActionButton
 * (action_button.h): configRowValueX is host-testable by include alone, and render() is a
 * template instantiated only against the device canvas.
 */

#ifndef ADVERSARY_CONFIG_ROW_H
#define ADVERSARY_CONFIG_ROW_H

#include "ui/theme.h"
#include <cstdint>
#include <cstring>

namespace adversary {
namespace ui {

/// Label x for a config row. Matches settings_screen's ROW_TEXT_X so the two biggest value
/// lists in the firmware share one left edge (ADR-0096).
constexpr int16_t CONFIG_ROW_LABEL_X = 8;

/// Gap from the canvas right edge to the value's right edge. Matches settings_screen's
/// VALUE_RIGHT_MARGIN: wide enough to clear the shared list's scrollbar (ADR-0096).
constexpr int16_t CONFIG_ROW_VALUE_MARGIN = 8;

/// x at which to start printing `value` so its right edge sits CONFIG_ROW_VALUE_MARGIN from
/// the canvas right edge. Pure so it is host-testable by include alone; a null/empty value
/// yields the margin position without dereferencing.
inline int16_t configRowValueX(int16_t screenWidth, const char* value) {
    const int16_t valWidth =
        static_cast<int16_t>(value ? std::strlen(value) : 0) * theme::FONT_CELL_WIDTH;
    return screenWidth - CONFIG_ROW_VALUE_MARGIN - valWidth;
}

class ConfigRow {
public:
    /// Draws one config row's content with its row top at `rowTopY`: the label left in
    /// TEXT_PRIMARY, the value right-aligned in dim TEXT_SECONDARY, both dropped by the house
    /// text baseline (theme::LIST_ROW_TEXT_DY). No band and no selection recolour (ADR-0096):
    /// the caller owns the selection band. An empty/null value prints nothing.
    template <typename Canvas>
    static void render(Canvas& canvas, int16_t rowTopY, const char* label, const char* value) {
        const int16_t textY = rowTopY + theme::LIST_ROW_TEXT_DY;

        canvas.setTextColor(theme::TEXT_PRIMARY());
        canvas.setCursor(CONFIG_ROW_LABEL_X, textY);
        canvas.print(label ? label : "");

        if (value && value[0]) {
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.setCursor(configRowValueX(canvas.width(), value), textY);
            canvas.print(value);
        }
    }
};

}  // namespace ui
}  // namespace adversary

#endif  // ADVERSARY_CONFIG_ROW_H
