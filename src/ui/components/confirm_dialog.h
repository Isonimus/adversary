/**
 * @file confirm_dialog.h
 * @brief Binary confirm-or-cancel popup, defaulting to Cancel (ADR-0085).
 *
 * The shared "confirm or cancel" modal for destructive and one-shot actions, and the sibling
 * primitive to ActionMenu (pick one of N). The selection defaults to Cancel, so a reflexive
 * Enter cancels rather than executing an irreversible action. Header-only like the other
 * popup siblings (text_input_popup.h, character_selector.h): the logic is host-testable by
 * include alone, and render() is a template instantiated only against the device sprite.
 */

#ifndef ADVERSARY_CONFIRM_DIALOG_H
#define ADVERSARY_CONFIRM_DIALOG_H

#include "ui/theme.h"
#include <cstdint>
#include <cstring>
#include <functional>

namespace adversary {
namespace ui {

class ConfirmDialog {
public:
    /// Warning: consequential but reversible (a transmit, enabling HID mode). Destructive:
    /// irreversible data loss; the question renders in ERROR and a "This cannot be undone."
    /// line is drawn.
    enum class Severity { Warning, Destructive };

    ConfirmDialog()
        : severity_(Severity::Warning)
        , detailCount_(0)
        , selectedIndex_(CANCEL_INDEX)
        , visible_(false) {
        question_[0] = '\0';
    }

    void setQuestion(const char* question) { copyText(question_, question, QUESTION_MAX); }
    void setSeverity(Severity severity) { severity_ = severity; }

    /// Drop all detail lines; call before repopulating each time the dialog is configured.
    void clearDetails() { detailCount_ = 0; }

    /// Add a detail line (an item identity like name/size/type, or an explanatory note). Up
    /// to DETAIL_MAX_LINES are kept; further lines are ignored. Copied into an owned buffer
    /// (char[] over String to avoid heap churn on this chip, per CLAUDE.md).
    void addDetail(const char* line) {
        if (detailCount_ >= DETAIL_MAX_LINES || !line) return;
        copyText(details_[detailCount_], line, DETAIL_TEXT_MAX);
        ++detailCount_;
    }

    void setOnConfirm(std::function<void()> callback) { onConfirm_ = callback; }
    void setOnCancel(std::function<void()> callback) { onCancel_ = callback; }

    /// Show the dialog with the selection reset to Cancel: the safe default (ADR-0085), even
    /// if a previous show left Confirm highlighted.
    void show() { visible_ = true; selectedIndex_ = CANCEL_INDEX; }
    void hide() { visible_ = false; }
    bool isVisible() const { return visible_; }

    /// Handle a key; returns true if consumed. ESC/backtick/N cancel; Enter acts on the
    /// highlighted option; Y confirms; `;`/`.` move the selection to Cancel/Confirm.
    bool handleInput(char key) {
        if (!visible_) return false;
        if (key == '`' || key == 27) { cancel(); return true; }  // 27 = ESC
        if (key == ';') { selectedIndex_ = CANCEL_INDEX; return true; }
        if (key == '.') { selectedIndex_ = CONFIRM_INDEX; return true; }
        if (key == '\n' || key == '\r') { selectCurrent(); return true; }
        if (key == 'y' || key == 'Y') { confirm(); return true; }
        if (key == 'n' || key == 'N') { cancel(); return true; }
        return false;
    }

    void selectCancel() { selectedIndex_ = CANCEL_INDEX; }
    void selectConfirm() { selectedIndex_ = CONFIRM_INDEX; }
    bool isConfirmSelected() const { return selectedIndex_ == CONFIRM_INDEX; }

    /// Whether render() draws the "This cannot be undone." line. Exposed so the severity
    /// contract is host-testable; render() uses this same predicate.
    bool showsUndoWarning() const { return severity_ == Severity::Destructive; }

    template <typename Canvas>
    void render(Canvas& canvas);

private:
    static constexpr int CANCEL_INDEX = 0;
    static constexpr int CONFIRM_INDEX = 1;
    static constexpr size_t QUESTION_MAX = 48;
    static constexpr size_t DETAIL_TEXT_MAX = 40;
    static constexpr int DETAIL_MAX_LINES = 3;

    static void copyText(char* dst, const char* src, size_t cap) {
        if (!src) { dst[0] = '\0'; return; }
        strncpy(dst, src, cap - 1);
        dst[cap - 1] = '\0';
    }

    void confirm() { visible_ = false; if (onConfirm_) onConfirm_(); }
    void cancel() { visible_ = false; if (onCancel_) onCancel_(); }
    void selectCurrent() { if (selectedIndex_ == CONFIRM_INDEX) confirm(); else cancel(); }

    // size-1 glyphs are 6 px wide on this display stack; used to center text as ActionMenu does.

    template <typename Canvas>
    static void drawCentered(Canvas& canvas, const char* text, int16_t boxX, int16_t boxWidth,
                             int16_t y) {
        const int16_t textWidth = static_cast<int16_t>(strlen(text)) * theme::FONT_CELL_WIDTH;
        canvas.setCursor(boxX + (boxWidth - textWidth) / 2, y);
        canvas.print(text);
    }

    char question_[QUESTION_MAX];
    char details_[DETAIL_MAX_LINES][DETAIL_TEXT_MAX];
    Severity severity_;
    int detailCount_;
    int selectedIndex_;
    bool visible_;
    std::function<void()> onConfirm_;
    std::function<void()> onCancel_;

    // Layout constants (shared shell with ActionMenu).
    static constexpr int16_t SCREEN_MARGIN = 20;   // per side, as ActionMenu's 20 px margin
    static constexpr int16_t MENU_PADDING = 8;
    static constexpr int16_t LINE_HEIGHT = 14;
    static constexpr int16_t BUTTON_HEIGHT = 18;
    static constexpr int16_t BUTTON_GAP = 12;
    static constexpr int16_t BUTTON_TEXT_PAD = 12;  // inner horizontal padding per button
    static constexpr int16_t BUTTON_TEXT_DY = 5;    // text baseline offset inside the band
    static constexpr int16_t BUTTON_TOP_GAP = 6;    // gap above the button row
    static constexpr int16_t BORDER_RADIUS = 4;
};

// ==================== Template Implementation ====================

template <typename Canvas>
void ConfirmDialog::render(Canvas& canvas) {
    if (!visible_) return;

    const int16_t screenWidth = canvas.width();
    const int16_t screenHeight = canvas.height();
    const int16_t menuWidth = screenWidth - SCREEN_MARGIN * 2;

    const int16_t contentRows =
        static_cast<int16_t>(1 + detailCount_ + (showsUndoWarning() ? 1 : 0));
    const int16_t menuHeight = static_cast<int16_t>(
        contentRows * LINE_HEIGHT + BUTTON_TOP_GAP + BUTTON_HEIGHT + MENU_PADDING * 2);

    const int16_t menuX = (screenWidth - menuWidth) / 2;
    const int16_t menuY = (screenHeight - menuHeight) / 2;

    // Darkened backdrop and the rounded modal box: the same shell as ActionMenu.
    canvas.fillRect(0, 0, screenWidth, screenHeight, theme::BG_PRIMARY());
    canvas.fillRoundRect(menuX, menuY, menuWidth, menuHeight, BORDER_RADIUS, theme::BG_SECONDARY());
    canvas.drawRoundRect(menuX, menuY, menuWidth, menuHeight, BORDER_RADIUS, theme::ACCENT());

    canvas.setTextSize(1);
    int16_t y = menuY + MENU_PADDING;

    // Question, coloured by severity.
    canvas.setTextColor(showsUndoWarning() ? theme::ERROR() : theme::WARNING());
    drawCentered(canvas, question_, menuX, menuWidth, y);
    y += LINE_HEIGHT;

    // Detail lines.
    canvas.setTextColor(theme::TEXT_PRIMARY());
    for (int i = 0; i < detailCount_; ++i) {
        drawCentered(canvas, details_[i], menuX, menuWidth, y);
        y += LINE_HEIGHT;
    }

    // Irreversibility warning for destructive actions.
    if (showsUndoWarning()) {
        canvas.setTextColor(theme::ERROR());
        drawCentered(canvas, "This cannot be undone.", menuX, menuWidth, y);
        y += LINE_HEIGHT;
    }

    y += BUTTON_TOP_GAP;

    // Cancel | Confirm, centered; the selected option carries the house BG_SELECTED() band.
    const char* labels[2] = {"Cancel", "Confirm"};
    int16_t widths[2];
    int16_t totalWidth = BUTTON_GAP;
    for (int i = 0; i < 2; ++i) {
        widths[i] = static_cast<int16_t>(strlen(labels[i])) * theme::FONT_CELL_WIDTH + BUTTON_TEXT_PAD;
        totalWidth += widths[i];
    }
    int16_t bx = menuX + (menuWidth - totalWidth) / 2;
    for (int i = 0; i < 2; ++i) {
        const bool selected = (i == selectedIndex_);
        if (selected) {
            canvas.fillRect(bx, y, widths[i], BUTTON_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        const int16_t labelWidth = static_cast<int16_t>(strlen(labels[i])) * theme::FONT_CELL_WIDTH;
        canvas.setCursor(bx + (widths[i] - labelWidth) / 2, y + BUTTON_TEXT_DY);
        canvas.print(labels[i]);
        bx += widths[i] + BUTTON_GAP;
    }
}

}  // namespace ui
}  // namespace adversary

#endif  // ADVERSARY_CONFIRM_DIALOG_H
