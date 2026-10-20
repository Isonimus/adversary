/**
 * @file action_button.h
 * @brief Full-width green "start an operation" action row (ADR-0087).
 *
 * The shared standalone start-action row, and the third sibling to ActionMenu (pick one of
 * N) and ConfirmDialog (confirm or cancel). It renders a full-width band row that keeps the
 * firmware's green "start/go" identity: unselected is green text on no band; selected fills
 * a fixed-green band and draws the label in BG_PRIMARY (near-black in every theme), which is
 * the fix for the old selected label that used the per-theme TEXT_PRIMARY and went
 * green-on-green in the green theme. Header-only like the popup siblings (confirm_dialog.h,
 * text_input_popup.h): formatActionLabel is host-testable by include alone, and render() is a
 * template instantiated only against the device canvas.
 *
 * Not in scope (ADR-0087): the config-form terminal action rows, which stay with the
 * config-form restyle (ledger item 91). This component is a render helper with no state and
 * no interaction model: the selection lives in each screen's existing prompt/config index.
 */

#ifndef ADVERSARY_ACTION_BUTTON_H
#define ADVERSARY_ACTION_BUTTON_H

#include "ui/theme.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace adversary {
namespace ui {

/// Framed-label buffer bound. The longest migrated verb is "START AUTO HUNT" (15), plus the
/// ">> " / " <<" frame (6) and the terminator; 48 leaves ample headroom for future verbs.
constexpr std::size_t ACTION_LABEL_MAX = 48;

/// Frames a bare verb into the house ">> VERB <<" decoration, so call sites pass "START AUDIT"
/// and the framing lives in one place. Truncates safely to `cap` and always null-terminates.
inline void formatActionLabel(char* out, std::size_t cap, const char* label) {
    if (cap == 0) return;
    std::snprintf(out, cap, ">> %s <<", label ? label : "");
}

class ActionButton {
public:
    /// Draws a full-width start-action row with its top at `y`. When `selected`, fills a
    /// theme::SUCCESS() band and draws the label in theme::BG_PRIMARY() for guaranteed
    /// contrast in every theme; otherwise draws theme::SUCCESS() text on no band. The band is
    /// theme::LIST_ROW_HEIGHT tall; the framed label is centred both ways (horizontally across
    /// the full width like a button, vertically by theme::LIST_ROW_TEXT_DY), so the row
    /// re-centres if the tokens change and the symmetric ">> <<" frame reads as deliberate.
    template <typename Canvas>
    static void render(Canvas& canvas, int16_t y, const char* label, bool selected) {
        const int16_t screenWidth = canvas.width();
        if (selected) {
            canvas.fillRect(0, y, screenWidth, theme::LIST_ROW_HEIGHT, theme::SUCCESS());
            canvas.setTextColor(theme::BG_PRIMARY());
        } else {
            canvas.setTextColor(theme::SUCCESS());
        }

        char framed[ACTION_LABEL_MAX];
        formatActionLabel(framed, sizeof(framed), label);

        const int16_t textWidth = static_cast<int16_t>(std::strlen(framed)) * theme::FONT_CELL_WIDTH;
        canvas.setTextSize(1);
        canvas.setCursor((screenWidth - textWidth) / 2, y + theme::LIST_ROW_TEXT_DY);
        canvas.print(framed);
    }
};

} // namespace ui
} // namespace adversary

#endif // ADVERSARY_ACTION_BUTTON_H
