/**
 * @file storage_screen.cpp
 * @brief StorageScreen implementation (ADR-0166 / slice-0167).
 */

#include "storage_screen.h"

#include "hal/storage/sd_manager.h"

#ifdef ESP32
#include <Arduino.h>
#include <SD.h>
#endif

#include <cstdio>
#include <cstring>

namespace adversary {

#ifdef ESP32
namespace {

// Arduino-SD-backed one-level iterator. Holds the current File across
// next() calls because arduino-esp32 owns the `name()` string pointer as
// long as its File is alive — closing it before the caller reads the name
// would invalidate the pointer. computeStorageRow() only reads size +
// isDirectory, but the interface returns a name too so other callers are
// not boxed in.
class SdFileLister : public IDirectoryLister {
public:
    bool open(const char* path) override {
        if (open_) root_.close();
        if (!SD.exists(path)) return false;
        root_ = SD.open(path);
        if (!root_) return false;
        if (!root_.isDirectory()) { root_.close(); return false; }
        open_ = true;
        return true;
    }

    bool next(DirEntry& out) override {
        if (!open_) return false;
        current_.close();
        current_ = root_.openNextFile();
        if (!current_) return false;
        out.name = current_.name();
        out.size = static_cast<uint32_t>(current_.size());
        out.isDirectory = current_.isDirectory();
        return true;
    }

    void close() override {
        current_.close();
        if (open_) {
            root_.close();
            open_ = false;
        }
    }

private:
    File root_;
    File current_;
    bool open_ = false;
};

void formatBytes(uint64_t bytes, char* out, size_t cap) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        snprintf(out, cap, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        snprintf(out, cap, "%.1f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(out, cap, "%.0f KB", bytes / 1024.0);
    } else {
        snprintf(out, cap, "%u B", static_cast<unsigned>(bytes));
    }
}

}  // namespace
#endif

StorageScreen::StorageScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , sdMounted_(false)
    , phase_(Phase::Loading)
    , loadingPrimed_(false)
    , loadIndex_(0)
    , cardTotalBytes_(0)
    , curatedTotalBytes_(0)
{
    // Selectable (not scroll-only): the row the operator is on is highlighted
    // so the list reads as navigable. There is no per-row action (read-only
    // overview, ADR-0166) — ENTER is a no-op and only ESC/backtick exits.
    for (auto& row : rows_) row = StorageRow{false, 0, 0, false};
}

StorageScreen::~StorageScreen() { deinit(); }

void StorageScreen::init() {}
void StorageScreen::deinit() { hide(); }

void StorageScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    loadIndex_ = 0;
    loadingPrimed_ = false;
    curatedTotalBytes_ = 0;
    cardTotalBytes_ = 0;
    for (auto& row : rows_) row = StorageRow{false, 0, 0, false};

#ifdef ESP32
    sdMounted_ = SDManager::getInstance().isReady();
    if (sdMounted_) {
        cardTotalBytes_ = SD.cardSize();
        phase_ = Phase::Loading;  // folders scanned one-per-frame in update()
    } else {
        phase_ = Phase::Ready;    // nothing to scan; drawNotMounted() handles it
    }
#else
    sdMounted_ = false;
    phase_ = Phase::Ready;
#endif

    scrollModel_.setItemCount(0);
    scrollModel_.setItemCount(static_cast<int>(CURATED_PATH_COUNT));
    footerHints_.setHints({{'`', "Back"}});
}

void StorageScreen::hide() { visible_ = false; }

void StorageScreen::update() {
    if (phase_ != Phase::Loading) return;
    // Let one "Loading..." frame paint before the first (possibly slow) scan,
    // otherwise the worst folder is walked before anything is drawn and the
    // carousel appears frozen.
    if (!loadingPrimed_) {
        loadingPrimed_ = true;
        needsRedraw_ = true;
        return;
    }
    stepWalk();
}

void StorageScreen::stepWalk() {
#ifdef ESP32
    if (!sdMounted_ || loadIndex_ >= CURATED_PATH_COUNT) {
        phase_ = Phase::Ready;
        return;
    }
    // Scan exactly one folder this frame, then yield so the UI repaints.
    SdFileLister lister;
    rows_[loadIndex_] = computeStorageRow(lister, CURATED_PATHS[loadIndex_].path);
    if (rows_[loadIndex_].ok) curatedTotalBytes_ += rows_[loadIndex_].totalBytes;
    ++loadIndex_;
    if (loadIndex_ >= CURATED_PATH_COUNT) phase_ = Phase::Ready;
    needsRedraw_ = true;
#else
    phase_ = Phase::Ready;
#endif
}

bool StorageScreen::handleInput(char key) {
    needsRedraw_ = true;

    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action == '`') {
            shouldExit_ = true;
        } else if (action == ';') {
            scrollModel_.moveUp();
        } else if (action == '.') {
            scrollModel_.moveDown();
        }
        return true;
    }
    switch (key) {
        case ';': scrollModel_.moveUp();   return true;
        case '.': scrollModel_.moveDown(); return true;
        case '`':
            shouldExit_ = true;
            return true;
        // ENTER is intentionally inert: this is a read-only overview with no
        // per-row action (ADR-0166). Swallow it so it never bubbles up to the
        // menu as an accidental exit.
        case '\n':
        case '\r':
            return true;
    }
    return false;
}

void StorageScreen::render(Canvas& canvas) {
    if (!visible_) return;
#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;

    canvas.fillScreen(theme::BG_PRIMARY());
    drawHeader(canvas);

    if (!sdMounted_) {
        drawNotMounted(canvas);
    } else if (phase_ == Phase::Loading) {
        drawLoading(canvas);
    } else {
        drawCapacityPanel(canvas, HEADER_HEIGHT + 2);
        drawRows(canvas, HEADER_HEIGHT + 2 + CAPACITY_PANEL_HEIGHT);
    }

    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void StorageScreen::drawLoading(Canvas& canvas) {
#ifdef ESP32
    canvas.setTextSize(1);
    int16_t x = 8;
    int16_t y = HEADER_HEIGHT + 20;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(x, y);
    canvas.print("Loading SD card data...");

    // Name the folder currently being scanned so a long handshakes sweep reads
    // as progress, not a hang.
    y += LINE_HEIGHT + 4;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    const size_t scanning = loadIndex_ < CURATED_PATH_COUNT ? loadIndex_
                                                            : CURATED_PATH_COUNT - 1;
    canvas.printf("Scanning %s (%u/%u)", CURATED_PATHS[scanning].label,
                  static_cast<unsigned>(loadIndex_ + 1),
                  static_cast<unsigned>(CURATED_PATH_COUNT));
#else
    (void)canvas;
#endif
}

void StorageScreen::drawHeader(Canvas& canvas) {
    ui::StatusBar::render(canvas, "STORAGE", nullptr,
                          theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
}

void StorageScreen::drawNotMounted(Canvas& canvas) {
#ifdef ESP32
    canvas.setTextSize(1);
    int16_t x = 8;
    int16_t y = HEADER_HEIGHT + 20;
    canvas.setTextColor(theme::WARNING());
    canvas.setCursor(x, y);
    canvas.print("SD not mounted");
    y += LINE_HEIGHT + 2;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("Insert a card and re-enter.");
#else
    (void)canvas;
#endif
}

void StorageScreen::drawCapacityPanel(Canvas& canvas, int16_t top) {
#ifdef ESP32
    char usedBuf[16], totalBuf[16];
    formatBytes(curatedTotalBytes_, usedBuf, sizeof usedBuf);
    formatBytes(cardTotalBytes_, totalBuf, sizeof totalBuf);

    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(4, top);
    canvas.printf("%s used / %s total", usedBuf, totalBuf);

    const int16_t barTop = top + LINE_HEIGHT + 2;
    const int16_t barX = 4;
    const int16_t barW = canvas.width() - 8;
    const int16_t barH = 6;
    canvas.drawRect(barX, barTop, barW, barH, theme::TEXT_DISABLED());
    if (cardTotalBytes_ > 0 && curatedTotalBytes_ > 0) {
        // The fill is "our data" against the card total. The delta from 100%
        // is intentionally not labelled — the Storage view only speaks for
        // paths it knows (ADR-0166).
        uint64_t fillW64 = static_cast<uint64_t>(barW - 2) * curatedTotalBytes_
                           / cardTotalBytes_;
        int16_t fillW = static_cast<int16_t>(fillW64);
        if (fillW > 0) {
            canvas.fillRect(barX + 1, barTop + 1, fillW, barH - 2, theme::ACCENT());
        }
    }
#else
    (void)canvas; (void)top;
#endif
}

void StorageScreen::drawRows(Canvas& canvas, int16_t top) {
#ifdef ESP32
    const int16_t availableHeight = canvas.height() - top - ui::FOOTER_HEIGHT;
    canvas.setTextSize(1);

    scrollModel_.setItemCount(static_cast<int>(CURATED_PATH_COUNT));
    ScrollableList::render(canvas, scrollModel_, 0, top, canvas.width(),
        availableHeight, LINE_HEIGHT,
        [this](Canvas& c, int idx, int16_t rowY, bool selected) {
            const CuratedPath& p = CURATED_PATHS[idx];
            const StorageRow& row = rows_[idx];

            if (selected) {
                c.fillRect(0, rowY - 1, c.width(), LINE_HEIGHT, theme::BG_SELECTED());
            }

            c.setTextColor(theme::TEXT_PRIMARY());
            c.setCursor(4, rowY);
            c.print(p.label);

            char rhs[28];
            uint16_t rhsColor;
            if (!row.ok) {
                snprintf(rhs, sizeof rhs, "--");
                rhsColor = theme::TEXT_DISABLED();
            } else {
                char bytesBuf[16];
                formatBytes(row.totalBytes, bytesBuf, sizeof bytesBuf);
                if (row.truncated) {
                    snprintf(rhs, sizeof rhs, "500+ files  %s", bytesBuf);
                    rhsColor = theme::WARNING();
                } else {
                    snprintf(rhs, sizeof rhs, "%u %s  %s",
                             static_cast<unsigned>(row.fileCount),
                             row.fileCount == 1 ? "file" : "files",
                             bytesBuf);
                    rhsColor = theme::TEXT_SECONDARY();
                }
            }
            c.setTextColor(rhsColor);
            int16_t rhsWidth = static_cast<int16_t>(strlen(rhs))
                             * theme::FONT_CELL_WIDTH;
            c.setCursor(c.width() - rhsWidth - 4, rowY);
            c.print(rhs);
        });
#else
    (void)canvas; (void)top;
#endif
}

}  // namespace adversary
