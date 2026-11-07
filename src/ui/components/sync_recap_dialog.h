/**
 * @file sync_recap_dialog.h
 * @brief End-of-run recap for a bulk cloud sync (ADR-0142, slice-0143).
 *
 * A bulk sync purges the canvas for the whole run, so the per-file toasts the
 * upload services fire are invisible and failures used to vanish behind a green
 * "Sync complete". This modal is the reliable end-of-run surface: per-service
 * tallies (sent / already / failed) and a scrollable list of the failed captures
 * with a short static reason each. It is shown AFTER the canvas is restored.
 *
 * Header-only like confirm_dialog.h: the tally / overflow / scroll logic is
 * host-testable by include alone, and render() is a template instantiated only
 * against the device sprite. Reasons are stored as const char* because the
 * upload path only ever hands us string literals (ADR-0142); SSIDs are copied.
 */

#ifndef ADVERSARY_SYNC_RECAP_DIALOG_H
#define ADVERSARY_SYNC_RECAP_DIALOG_H

#include "ui/theme.h"
#include "modules/network/upload_result.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>

namespace adversary {
namespace ui {

class SyncRecapDialog {
public:
    enum class Service : uint8_t { WpaSec = 0, Pwncrack = 1 };

    SyncRecapDialog() { reset(); }

    /// Clear every tally and the failure list, and hide. Call when a run starts.
    void reset() {
        for (int i = 0; i < SERVICE_COUNT; ++i) { sent_[i] = already_[i] = failed_[i] = 0; }
        failCount_ = 0;
        scrollOffset_ = 0;
        stopped_ = false;
        visible_ = false;
    }

    /// Fold one job's outcome into the tallies. For a failure, also append it to
    /// the capped list (ssid copied; reason must be a static literal, or null).
    void record(Service svc, UploadOutcome outcome, const char* ssid, const char* reason) {
        const int i = index(svc);
        switch (outcome) {
            case UploadOutcome::Sent:            ++sent_[i];    break;
            case UploadOutcome::AlreadyUploaded: ++already_[i]; break;
            case UploadOutcome::Failed:
                ++failed_[i];
                if (failCount_ < FAILED_MAX) {
                    Failure& f = failures_[failCount_++];
                    f.svc = svc;
                    copyText(f.ssid, ssid, SSID_MAX);
                    f.reason = (reason && reason[0]) ? reason : "failed";
                }
                break;
        }
    }

    /// Mark the run as ESC-stopped (changes the title; the tally stands).
    void setStopped(bool stopped) { stopped_ = stopped; }

    void show() { visible_ = true; scrollOffset_ = 0; }
    void hide() { visible_ = false; }
    bool isVisible() const { return visible_; }

    void setOnDismiss(std::function<void()> cb) { onDismiss_ = cb; }

    /// Blocking modal: consumes every key while visible. Enter/ESC/backtick close;
    /// `;` / `.` scroll the failure list when it overflows the visible rows.
    bool handleInput(char key) {
        if (!visible_) return false;
        if (key == '\n' || key == '\r' || key == '`' || key == 27) {  // 27 = ESC
            visible_ = false;
            if (onDismiss_) onDismiss_();
            return true;
        }
        if (key == ';') { if (scrollOffset_ > 0) --scrollOffset_; return true; }
        if (key == '.') { if (scrollOffset_ < maxScrollOffset()) ++scrollOffset_; return true; }
        return true;  // swallow anything else so it can't leak to the screen
    }

    // ---- Accessors (host-testable state) --------------------------------------
    int sent(Service s) const { return sent_[index(s)]; }
    int already(Service s) const { return already_[index(s)]; }
    int failed(Service s) const { return failed_[index(s)]; }
    int totalFailed() const { return failed_[0] + failed_[1]; }
    int listedFailures() const { return failCount_; }             // stored (capped)
    int overflow() const { return totalFailed() - failCount_; }   // dropped past the cap
    bool stopped() const { return stopped_; }
    int scrollOffset() const { return scrollOffset_; }
    int maxScrollOffset() const {
        const int hidden = failCount_ - VISIBLE_FAIL_ROWS;
        return hidden > 0 ? hidden : 0;
    }

    template <typename Canvas>
    void render(Canvas& canvas);

private:
    static constexpr int SERVICE_COUNT = 2;
    static constexpr int FAILED_MAX = 16;        // stored failures; the rest count as overflow
    static constexpr int VISIBLE_FAIL_ROWS = 4;  // failure rows shown at once (scroll for more)
    static constexpr size_t SSID_MAX = 33;
    static constexpr size_t ROW_TEXT_MAX = 34;   // "ssid (svc): reason", truncated to the
                                                 // box inner width (~33 chars at 6 px/glyph in
                                                 // the 240 px-wide modal); full text on serial

    struct Failure {
        Service svc;
        char ssid[SSID_MAX];
        const char* reason;  // static literal (ADR-0142)
        Failure() : svc(Service::WpaSec), reason("failed") { ssid[0] = '\0'; }
    };

    static int index(Service s) { return static_cast<int>(s); }
    static const char* label(Service s) { return s == Service::WpaSec ? "WPA-SEC" : "pwncrack"; }

    static void copyText(char* dst, const char* src, size_t cap) {
        if (!src) { dst[0] = '\0'; return; }
        strncpy(dst, src, cap - 1);
        dst[cap - 1] = '\0';
    }

    int sent_[SERVICE_COUNT];
    int already_[SERVICE_COUNT];
    int failed_[SERVICE_COUNT];
    Failure failures_[FAILED_MAX];
    int failCount_;
    int scrollOffset_;
    bool stopped_;
    bool visible_;
    std::function<void()> onDismiss_;

    // Layout (shares the ActionMenu / ConfirmDialog shell). Sized so the tallest
    // state (title + 2 service lines + list header + VISIBLE_FAIL_ROWS + overflow
    // + footer = 10 lines) fits the 135 px Cardputer screen: 10*12 + 2*6 = 132.
    static constexpr int16_t SCREEN_MARGIN = 14;
    static constexpr int16_t PADDING = 6;
    static constexpr int16_t LINE_HEIGHT = 12;
    static constexpr int16_t BORDER_RADIUS = 4;
};

// ==================== Template Implementation ====================

template <typename Canvas>
void SyncRecapDialog::render(Canvas& canvas) {
    if (!visible_) return;

    const int16_t screenWidth = canvas.width();
    const int16_t screenHeight = canvas.height();
    const int16_t boxX = SCREEN_MARGIN;
    const int16_t boxWidth = screenWidth - SCREEN_MARGIN * 2;

    const int visibleFails = (failCount_ < VISIBLE_FAIL_ROWS) ? failCount_ : VISIBLE_FAIL_ROWS;
    // Title + 2 service lines + a "failures/none" line + the visible failure rows
    // + an overflow/scroll line + the footer.
    const int contentLines = 1 + SERVICE_COUNT + 1 + visibleFails + 1 + 1;
    const int16_t boxHeight =
        static_cast<int16_t>(contentLines * LINE_HEIGHT + PADDING * 2);
    const int16_t boxY = (screenHeight - boxHeight) / 2 > 0 ? (screenHeight - boxHeight) / 2 : 0;

    canvas.fillRect(0, 0, screenWidth, screenHeight, theme::BG_PRIMARY());
    canvas.fillRoundRect(boxX, boxY, boxWidth, boxHeight, BORDER_RADIUS, theme::BG_SECONDARY());
    canvas.drawRoundRect(boxX, boxY, boxWidth, boxHeight, BORDER_RADIUS, theme::ACCENT());

    canvas.setTextSize(1);
    int16_t y = boxY + PADDING;
    const int16_t textX = boxX + PADDING;

    // Title, coloured by the worst outcome.
    const bool anyFail = totalFailed() > 0;
    canvas.setTextColor(stopped_ ? theme::WARNING()
                                 : (anyFail ? theme::ERROR() : theme::SUCCESS()));
    char title[40];
    if (stopped_) {
        snprintf(title, sizeof(title), "SYNC STOPPED (%d failed)", totalFailed());
    } else if (anyFail) {
        snprintf(title, sizeof(title), "SYNC DONE - %d FAILED", totalFailed());
    } else {
        snprintf(title, sizeof(title), "SYNC COMPLETE");
    }
    canvas.setCursor(textX, y);
    canvas.print(title);
    y += LINE_HEIGHT;

    // Per-service tallies.
    canvas.setTextColor(theme::TEXT_PRIMARY());
    for (int i = 0; i < SERVICE_COUNT; ++i) {
        char line[40];
        snprintf(line, sizeof(line), "%-8s %d sent %d skip %d fail",
                 label(static_cast<Service>(i)), sent_[i], already_[i], failed_[i]);
        canvas.setCursor(textX, y);
        canvas.print(line);
        y += LINE_HEIGHT;
    }

    // Failure list header + rows.
    if (failCount_ == 0) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(textX, y);
        canvas.print("No failures.");
        y += LINE_HEIGHT;
    } else {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(textX, y);
        canvas.print("Failed:");
        y += LINE_HEIGHT;

        canvas.setTextColor(theme::ERROR());
        for (int r = 0; r < visibleFails; ++r) {
            const int idx = scrollOffset_ + r;
            if (idx >= failCount_) break;
            const Failure& f = failures_[idx];
            char row[ROW_TEXT_MAX];
            snprintf(row, sizeof(row), "%s (%s): %s", f.ssid, label(f.svc), f.reason);
            canvas.setCursor(textX, y);
            canvas.print(row);
            y += LINE_HEIGHT;
        }
    }

    // Overflow / scroll hint line.
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(textX, y);
    if (overflow() > 0) {
        char more[40];
        snprintf(more, sizeof(more), "+%d more (see serial log)", overflow());
        canvas.print(more);
    } else if (maxScrollOffset() > 0) {
        canvas.print(";/. scroll");
    }
    y += LINE_HEIGHT;

    // Footer.
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(textX, y);
    canvas.print("Enter/`: close");
}

}  // namespace ui
}  // namespace adversary

#endif  // ADVERSARY_SYNC_RECAP_DIALOG_H
