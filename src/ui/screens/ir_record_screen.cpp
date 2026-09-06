/**
 * @file ir_record_screen.cpp
 * @brief IrRecordScreen — IR learn/replay console (slice-0004).
 */

#include "ir_record_screen.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../hal/storage/sd_manager.h"
#include "../../modules/ir/ir_capture.h"
#include "../../modules/ir/ir_replay.h"
#include "../../modules/ir/ir_capability.h"
#include "../../modules/storage/settings_manager.h"
#include "../components/toast_manager.h"
#include "../components/pulse_strip.h"

#ifdef ESP32
#include <Arduino.h>
#include <SD.h>
#include <IRutils.h>
#endif

namespace adversary {

namespace {

const char* txSourceLabel(ir::IrTxSource src) {
    return src == ir::IrTxSource::CapArray ? "Cap array" : "Built-in";
}

// Bound for a formatted feedback/description line; toasts truncate at 63.
constexpr size_t MESSAGE_BUF = 64;

// Move a wrapping list cursor with ;/. and follow it with the scroll window.
// Returns true iff @p key was an up/down key. (Same idiom as RadioScreen.)
bool listNav(char key, int& selection, int count, int& scroll, int visible) {
    if (count <= 0) return false;
    if (key == ';') {
        selection = (selection - 1 + count) % count;
    } else if (key == '.') {
        selection = (selection + 1) % count;
    } else {
        return false;
    }
    if (selection < scroll) scroll = selection;
    else if (selection >= scroll + visible) scroll = selection - visible + 1;
    return true;
}

}  // namespace

IrRecordScreen::IrRecordScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , captureAvailable_(false)
    , state_(MenuState::MAIN)
    , lastHintState_(MenuState::MAIN)
    , mainSelection_(0)
    , actionSelection_(0)
    , fileSelection_(0)
    , fileScroll_(0)
    , txSelection_(0)
    , captureStage_(0)
    , actionSignalValid_(false)
{
}

IrRecordScreen::~IrRecordScreen() {
    deinit();
}

void IrRecordScreen::init() {}

void IrRecordScreen::deinit() {
    hide();
}

ir::IrTxSource IrRecordScreen::txSource() const {
    return SettingsManager::getInstance().get().wireless.irTxSource;
}

void IrRecordScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    state_ = MenuState::MAIN;
    mainSelection_ = 0;
    captureStage_ = 0;

    captureAvailable_ = ir::hasIrRx();

    lastHintState_ = state_;
    updateFooterHints();
}

void IrRecordScreen::hide() {
    visible_ = false;
}

// Footer hints follow the state so the operator always sees the live actions
// (UI-STYLE: FooterHints::handleInputWithDispatch + per-state setHints).
void IrRecordScreen::updateFooterHints() {
    switch (state_) {
        case MenuState::MAIN:
            footerHints_.setHints({{' ', "Focus"}, {';', "Nav"}, {'\n', "Select"}, {'`', "Back"}});
            break;
        case MenuState::CODE_LIST:
            footerHints_.setHints({{' ', "Focus"}, {';', "Nav"},
                                   {'\n', "Open", !codes_.empty()}, {'`', "Back"}});
            break;
        case MenuState::CODE_ACTION:
        case MenuState::TX_SOURCE_SELECT:
            footerHints_.setHints({{' ', "Focus"}, {';', "Nav"}, {'\n', "Select"}, {'`', "Back"}});
            break;
        case MenuState::REPLAY_CONFIRM:
        case MenuState::DELETE_CONFIRM:
            footerHints_.setHints({{'Y', "Yes"}, {'N', "No"}, {'`', "Cancel"}});
            break;
        case MenuState::CAPTURE_REVIEW:
            footerHints_.setHints({{'S', "Save"}, {'`', "Discard"}});
            break;
        case MenuState::CAPTURING:
        case MenuState::NAMING:
            footerHints_.setHints({});  // blocking / popup owns the input
            break;
    }
}

void IrRecordScreen::fullPath(const char* name, char* out, size_t outSize) const {
    snprintf(out, outSize, "%s/%s", config::SD_IR_PATH, name);
}

void IrRecordScreen::describeSignal(const ir::IrSignal& sig, char* out,
                                    size_t outSize) const {
    if (sig.encoding == ir::IrEncoding::Parsed) {
#ifdef ESP32
        snprintf(out, outSize, "%s %u bit 0x%llX",
                 typeToString(static_cast<decode_type_t>(sig.protocol), false).c_str(),
                 static_cast<unsigned>(sig.bits),
                 static_cast<unsigned long long>(sig.value));
#else
        snprintf(out, outSize, "proto %u %u bit 0x%llX", static_cast<unsigned>(sig.protocol),
                 static_cast<unsigned>(sig.bits),
                 static_cast<unsigned long long>(sig.value));
#endif
    } else {
        snprintf(out, outSize, "RAW %u edges",
                 static_cast<unsigned>(sig.timingsUs.size()));
    }
}

// --- Capture -----------------------------------------------------------------

void IrRecordScreen::startCapture() {
    state_ = MenuState::CAPTURING;
    captureStage_ = 1;  // let "Listening" paint before the blocking RX window
    needsRedraw_ = true;
}

void IrRecordScreen::performCapture() {
    ir::IrSignal sig;
    if (ir::captureIrSignal(CAPTURE_WINDOW_MS, sig)) {
        char summary[MESSAGE_BUF];
        describeSignal(sig, summary, sizeof(summary));
#ifdef ESP32
        Serial.printf("[IR] Captured %s\n", summary);
#endif
        pendingSignal_ = sig;
        // Show the pulse-strip preview first so the operator can judge the
        // capture and discard a ragged/incomplete one before naming it.
        state_ = MenuState::CAPTURE_REVIEW;
    } else {
#ifdef ESP32
        Serial.println("[IR] No frame captured (RX window timed out)");
#endif
        showErrorToast("Capture failed, retry closer");
        state_ = MenuState::MAIN;
    }
    captureStage_ = 0;
    needsRedraw_ = true;
}

void IrRecordScreen::promptForName() {
    namePopup_.setOnSubmit([this](const char* text) { saveCapturedCode(text); });
    namePopup_.setOnCancel([this]() {
        showToast("Capture discarded");
        state_ = MenuState::MAIN;
    });
    namePopup_.show("Code name", "ir", false, NAME_MAX_LEN);
    state_ = MenuState::NAMING;
    needsRedraw_ = true;
}

void IrRecordScreen::saveCapturedCode(const char* name) {
    // Sanitise: strip path separators, bound to NAME_MAX_LEN, reject empty.
    char safe[NAME_MAX_LEN + 1];
    size_t j = 0;
    for (size_t i = 0; name[i] != '\0' && j < NAME_MAX_LEN; ++i) {
        char c = name[i];
        safe[j++] = (c == '/' || c == '\\') ? '_' : c;
    }
    safe[j] = '\0';
    if (j == 0) {
        showWarningToast("Name required");
        state_ = MenuState::MAIN;
        needsRedraw_ = true;
        return;
    }

    std::string text;
    if (!ir::serializeIrSignal(pendingSignal_, text)) {
        showErrorToast("Serialize failed");
        state_ = MenuState::MAIN;
        needsRedraw_ = true;
        return;
    }

    SDManager& sd = SDManager::getInstance();
    sd.createDirectory(config::SD_IR_PATH);

    char path[128];
    snprintf(path, sizeof(path), "%s/%s.ir", config::SD_IR_PATH, safe);
    const FileResult result = sd.writeFile(
        path, reinterpret_cast<const uint8_t*>(text.data()), text.size());
    if (result.success) {
        char message[MESSAGE_BUF];
        snprintf(message, sizeof(message), "Saved %s.ir", safe);
        showSuccessToast(message);
    } else {
        showErrorToast("Save failed");
    }
    state_ = MenuState::MAIN;
    needsRedraw_ = true;
}

// --- Saved-code list ---------------------------------------------------------

void IrRecordScreen::loadCodeList() {
    codes_.clear();
#ifdef ESP32
    File dir = SD.open(config::SD_IR_PATH);
    if (dir && dir.isDirectory()) {
        File entry;
        while ((entry = dir.openNextFile())) {
            if (!entry.isDirectory()) {
                const char* raw = entry.name();
                const char* slash = strrchr(raw, '/');
                const char* base = slash ? slash + 1 : raw;  // basename, not path
                if (strstr(base, ".ir") != nullptr) {
                    CodeEntry item;
                    strncpy(item.name, base, sizeof(item.name) - 1);
                    item.name[sizeof(item.name) - 1] = '\0';
                    item.size = entry.size();
                    codes_.push_back(item);
                }
            }
            entry.close();
        }
        dir.close();
    }
#endif
}

void IrRecordScreen::loadActionSignal() {
    // Read + decode the selected code so the detail screen can show its encoding
    // and preview. A failure here is non-fatal for the screen (the actions still
    // work and replay re-reads with its own guards), so we only flag it and let
    // the draw fall back to a placeholder — no toast on mere navigation.
    actionSignalValid_ = false;
    if (fileSelection_ < 0 || fileSelection_ >= static_cast<int>(codes_.size())) {
        return;
    }
    char path[128];
    fullPath(codes_[fileSelection_].name, path, sizeof(path));

    SDManager& sd = SDManager::getInstance();
    const size_t size = sd.getFileSize(path);
    constexpr size_t IR_FILE_MAX = 64 * 1024;
    if (size == 0 || size > IR_FILE_MAX) return;

    std::vector<char> buffer(size);
    const FileResult result = sd.readFile(
        path, reinterpret_cast<uint8_t*>(buffer.data()), buffer.size());
    if (result.success &&
        ir::deserializeIrSignal(buffer.data(), result.bytesRead, actionSignal_)) {
        actionSignalValid_ = true;
    }
}

void IrRecordScreen::replaySelected() {
    if (fileSelection_ < 0 || fileSelection_ >= static_cast<int>(codes_.size())) {
        state_ = MenuState::CODE_LIST;
        return;
    }
    char path[128];
    fullPath(codes_[fileSelection_].name, path, sizeof(path));

    SDManager& sd = SDManager::getInstance();
    const size_t size = sd.getFileSize(path);
    // A well-formed .ir file is a small header plus a bounded timing list; a
    // wildly large file is corrupt. Bound the read generously but finitely.
    constexpr size_t IR_FILE_MAX = 64 * 1024;
    if (size == 0 || size > IR_FILE_MAX) {
        showErrorToast("Bad file size");
        state_ = MenuState::CODE_ACTION;
        needsRedraw_ = true;
        return;
    }
    std::vector<char> buffer(size);
    const FileResult result = sd.readFile(
        path, reinterpret_cast<uint8_t*>(buffer.data()), buffer.size());
    ir::IrSignal sig;
    if (!result.success ||
        !ir::deserializeIrSignal(buffer.data(), result.bytesRead, sig)) {
        showErrorToast("Corrupt code");
        state_ = MenuState::CODE_ACTION;
        needsRedraw_ = true;
        return;
    }
    const bool ok = ir::replayIrSignal(sig, ir::irTxPin(txSource()));
    if (ok) showSuccessToast("Replayed");
    else showErrorToast("Replay failed");
    state_ = MenuState::CODE_ACTION;
    needsRedraw_ = true;
}

void IrRecordScreen::deleteSelected() {
#ifdef ESP32
    if (fileSelection_ < 0 || fileSelection_ >= static_cast<int>(codes_.size())) {
        state_ = MenuState::CODE_LIST;
        return;
    }
    char path[128];
    fullPath(codes_[fileSelection_].name, path, sizeof(path));
    if (SD.remove(path)) showSuccessToast("Deleted");
    else showErrorToast("Delete failed");

    loadCodeList();
    if (fileSelection_ >= static_cast<int>(codes_.size())) {
        fileSelection_ = codes_.empty() ? 0 : static_cast<int>(codes_.size()) - 1;
    }
    fileScroll_ = 0;
    state_ = MenuState::CODE_LIST;
    needsRedraw_ = true;
#endif
}

// --- Frame update ------------------------------------------------------------

void IrRecordScreen::update() {
    if (namePopup_.isVisible()) return;
    if (state_ == MenuState::CAPTURING) {
        if (captureStage_ < 2) {
            captureStage_++;  // give render a frame to show "Listening"
            needsRedraw_ = true;
        } else if (captureStage_ == 2) {
            captureStage_ = 3;
            performCapture();
        }
    }
}

// --- Input -------------------------------------------------------------------

bool IrRecordScreen::handleInput(char key) {
    needsRedraw_ = true;

    if (namePopup_.isVisible()) {
        namePopup_.handleInput(key);  // its callbacks move state_
        return true;
    }

    if (key == 0x1B) key = '`';  // ESC is an alias for Back/Cancel

    // Footer interaction (space focus, ;/. cycle, Enter dispatch). When the footer
    // is unfocused this returns false and the state keys below run as list nav.
    char footerAction = 0;
    if (footerHints_.handleInputWithDispatch(key, footerAction)) {
        if (footerAction == 0) return true;  // focus toggle / footer navigation
        key = footerAction;                  // run the chosen action through the switch
    }

    int unusedScroll = 0;
    switch (state_) {
        case MenuState::MAIN:
            if (listNav(key, mainSelection_, 3, unusedScroll, 3)) return true;
            if (key == '\n' || key == '\r') {
                if (mainSelection_ == 0) {
                    if (captureAvailable_) {
                        startCapture();
                    } else {
                        showWarningToast("IR RX needs external module");
                    }
                } else if (mainSelection_ == 1) {
                    loadCodeList();
                    fileSelection_ = 0;
                    fileScroll_ = 0;
                    state_ = MenuState::CODE_LIST;
                } else {
                    txSelection_ = static_cast<int>(txSource());
                    state_ = MenuState::TX_SOURCE_SELECT;
                }
            } else if (key == '`') {
                shouldExit_ = true;
            }
            return true;

        case MenuState::CODE_LIST:
            if (listNav(key, fileSelection_, static_cast<int>(codes_.size()),
                        fileScroll_, VISIBLE_ROWS)) {
                return true;
            }
            if ((key == '\n' || key == '\r') && !codes_.empty()) {
                actionSelection_ = 0;
                loadActionSignal();
                state_ = MenuState::CODE_ACTION;
            } else if (key == '`') {
                state_ = MenuState::MAIN;
            }
            return true;

        case MenuState::CODE_ACTION:
            if (listNav(key, actionSelection_, 2, unusedScroll, 2)) return true;
            if (key == '\n' || key == '\r') {
                state_ = (actionSelection_ == 0) ? MenuState::REPLAY_CONFIRM
                                                 : MenuState::DELETE_CONFIRM;
            } else if (key == '`') {
                state_ = MenuState::CODE_LIST;
            }
            return true;

        case MenuState::REPLAY_CONFIRM:
            if (key == 'y' || key == 'Y') replaySelected();
            else if (key == 'n' || key == 'N' || key == '`') state_ = MenuState::CODE_ACTION;
            return true;

        case MenuState::DELETE_CONFIRM:
            if (key == 'y' || key == 'Y') deleteSelected();
            else if (key == 'n' || key == 'N' || key == '`') state_ = MenuState::CODE_ACTION;
            return true;

        case MenuState::TX_SOURCE_SELECT:
            if (listNav(key, txSelection_, 2, unusedScroll, 2)) return true;
            if (key == '\n' || key == '\r') {
                SettingsManager& sm = SettingsManager::getInstance();
                sm.getMutable().wireless.irTxSource =
                    static_cast<ir::IrTxSource>(txSelection_);
                sm.save();
                showSuccessToast("TX source saved");
                state_ = MenuState::MAIN;
            } else if (key == '`') {
                state_ = MenuState::MAIN;
            }
            return true;

        case MenuState::CAPTURE_REVIEW:
            if (key == '\n' || key == '\r' || key == 's' || key == 'S') {
                promptForName();
            } else if (key == '`') {
                showToast("Capture discarded");
                state_ = MenuState::MAIN;
            }
            return true;

        case MenuState::CAPTURING:
        case MenuState::NAMING:
            return true;  // blocking / popup-owned
    }
    return true;
}

// --- Render ------------------------------------------------------------------

void IrRecordScreen::render(Canvas& canvas) {
    if (!visible_) return;

#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;

    if (state_ != lastHintState_) {
        updateFooterHints();
        lastHintState_ = state_;
    }

    canvas.fillScreen(theme::BG_PRIMARY());
    ui::StatusBar::render(canvas, "IR RECORD", nullptr, theme::BG_SECONDARY(),
                          theme::TEXT_PRIMARY());

    switch (state_) {
        case MenuState::MAIN:
        case MenuState::NAMING:      drawMain(canvas); break;
        case MenuState::CAPTURING:   drawCapturing(canvas); break;
        case MenuState::CAPTURE_REVIEW: drawCaptureReview(canvas); break;
        case MenuState::CODE_LIST:   drawCodeList(canvas); break;
        case MenuState::CODE_ACTION: drawCodeAction(canvas); break;
        case MenuState::REPLAY_CONFIRM:
            drawConfirm(canvas, "Transmit this code?");
            break;
        case MenuState::DELETE_CONFIRM:
            drawConfirm(canvas, "Delete this code?");
            break;
        case MenuState::TX_SOURCE_SELECT: drawTxSourceSelect(canvas); break;
    }

    footerHints_.render(canvas);
    if (namePopup_.isVisible()) namePopup_.render(canvas);
#else
    (void)canvas;
#endif
}

#ifdef ESP32

void IrRecordScreen::drawMain(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(x, y);
    canvas.printf("IR   TX: %s", txSourceLabel(txSource()));
    y += ROW_HEIGHT;

    const char* items[3] = {"Learn (capture)", "Saved codes", "TX source"};
    for (int i = 0; i < 3; ++i) {
        const bool learnDisabled = (i == 0 && !captureAvailable_);
        if (i == mainSelection_) {
            canvas.fillRect(0, y - 2, canvas.width(), ROW_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(learnDisabled ? theme::TEXT_DISABLED()
                                              : theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(learnDisabled ? theme::TEXT_DISABLED()
                                              : theme::TEXT_SECONDARY());
        }
        canvas.setCursor(x, y);
        canvas.print(items[i]);
        y += ROW_HEIGHT;
    }
}

void IrRecordScreen::drawCapturing(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 10;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::SUCCESS());
    canvas.setCursor(x, y);
    canvas.print("Listening for IR");
    y += ROW_HEIGHT;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("Aim the remote, press...");
    y += ROW_HEIGHT;
    canvas.setCursor(x, y);
    canvas.printf("Window %lus",
                  static_cast<unsigned long>(CAPTURE_WINDOW_MS / 1000));
}

void IrRecordScreen::drawEncodingAndStrip(Canvas& canvas, const ir::IrSignal& sig,
                                          int16_t x, int16_t y, int16_t w) {
    constexpr int16_t PREVIEW_STRIP_H = 18;

    char summary[MESSAGE_BUF];
    describeSignal(sig, summary, sizeof(summary));
    const bool raw = (sig.encoding == ir::IrEncoding::Raw);
    canvas.setTextSize(1);
    // RAW is the lower-confidence fallback encoding, so dim it; a decoded
    // protocol reads in the primary colour (scanner convention: the multi-valued
    // type lives in the info line, not a badge).
    canvas.setTextColor(raw ? theme::TEXT_SECONDARY() : theme::TEXT_PRIMARY());
    canvas.setCursor(x, y);
    canvas.printf("%.30s", summary);

    // Only a raw train has edges to draw. A decoded (Parsed) code stores no
    // timing train (slice-0004), so the summary above IS its representation —
    // the preview degrades to that rather than fabricating a waveform.
    if (raw && !sig.timingsUs.empty()) {
        // IR raw is mark-first (timingsUs[0] is a mark), so firstLevelHigh=true.
        ui::renderPulseStrip(canvas, sig.timingsUs.data(), sig.timingsUs.size(),
                             /*firstLevelHigh=*/true, x, y + 12, w, PREVIEW_STRIP_H);
    }
}

void IrRecordScreen::drawCaptureReview(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::SUCCESS());
    canvas.setCursor(x, y);
    canvas.print("Captured - review");
    y += ROW_HEIGHT;
    drawEncodingAndStrip(canvas, pendingSignal_, x, y,
                         static_cast<int16_t>(canvas.width() - 2 * theme::PADDING_MD));
}

void IrRecordScreen::drawCodeList(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT;
    canvas.setTextSize(1);
    if (codes_.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(x, y + 6);
        canvas.print("No saved codes");
        return;
    }
    int end = fileScroll_ + VISIBLE_ROWS;
    if (end > static_cast<int>(codes_.size())) end = static_cast<int>(codes_.size());
    for (int i = fileScroll_; i < end; ++i) {
        if (i == fileSelection_) {
            canvas.fillRect(0, y, canvas.width(), ROW_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        canvas.setCursor(x, y + 4);
        canvas.printf("%.26s", codes_[i].name);
        y += ROW_HEIGHT;
    }
}

void IrRecordScreen::drawCodeAction(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(x, y);
    canvas.printf("%.26s", codes_[fileSelection_].name);
    y += ROW_HEIGHT;

    const char* items[2] = {"Replay", "Delete"};
    for (int i = 0; i < 2; ++i) {
        if (i == actionSelection_) {
            canvas.fillRect(0, y - 2, canvas.width(), ROW_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(i == 1 ? theme::WARNING() : theme::TEXT_SECONDARY());
        }
        canvas.setCursor(x, y);
        canvas.print(items[i]);
        y += ROW_HEIGHT;
    }

    // Encoding + preview of the selected code, below the actions.
    if (actionSignalValid_) {
        drawEncodingAndStrip(canvas, actionSignal_, x, y + 2,
                             static_cast<int16_t>(canvas.width() - 2 * theme::PADDING_MD));
    } else {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(x, y + 2);
        canvas.print("Encoding unavailable");
    }
}

void IrRecordScreen::drawConfirm(Canvas& canvas, const char* question) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 10;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::WARNING());
    canvas.setCursor(x, y);
    canvas.print(question);
    y += ROW_HEIGHT;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(x, y);
    canvas.printf("%.26s", codes_[fileSelection_].name);
    // The Yes/No/Cancel actions live in the footer hints (updateFooterHints).
}

void IrRecordScreen::drawTxSourceSelect(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("Transmit via");
    y += ROW_HEIGHT;
    const ir::IrTxSource sources[2] = {ir::IrTxSource::BuiltIn, ir::IrTxSource::CapArray};
    for (int i = 0; i < 2; ++i) {
        if (i == txSelection_) {
            canvas.fillRect(0, y - 2, canvas.width(), ROW_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        canvas.setCursor(x, y);
        canvas.print(txSourceLabel(sources[i]));
        y += ROW_HEIGHT;
    }
}

#else  // !ESP32 — draw helpers are firmware-only (Canvas is the device sprite)

void IrRecordScreen::drawMain(Canvas&) {}
void IrRecordScreen::drawCapturing(Canvas&) {}
void IrRecordScreen::drawCaptureReview(Canvas&) {}
void IrRecordScreen::drawEncodingAndStrip(Canvas&, const ir::IrSignal&, int16_t,
                                          int16_t, int16_t) {}
void IrRecordScreen::drawCodeList(Canvas&) {}
void IrRecordScreen::drawCodeAction(Canvas&) {}
void IrRecordScreen::drawConfirm(Canvas&, const char*) {}
void IrRecordScreen::drawTxSourceSelect(Canvas&) {}

#endif  // ESP32

} // namespace adversary
