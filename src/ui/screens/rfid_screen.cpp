/**
 * @file rfid_screen.cpp
 * @brief RfidScreen implementation
 */

#include "rfid_screen.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/toast_manager.h"
#include "utils/log.h"
#include <SD.h>

extern void adversary_ui_render_forced();

namespace adversary {

namespace {
constexpr const char* RFID_CAPTURES_DIR = "/adversary/captures/RFID";
// Rows the write-dump picker shows at once. Sized for its own layout (list starts just under the
// status bar), not the taller audit-summary header getVisibleSummaryItems() assumes (slice-0043).
constexpr int WRITE_PICK_VISIBLE_ROWS = 5;
// A MIFARE Classic 1K card has 16 sectors; the audit summary always lists exactly this many.
constexpr int MIFARE_1K_SECTORS = 16;
}

bool RfidScreen::s_writeModeRequested = false;

RfidScreen::RfidScreen()
    : m_manager(RFIDManager::getInstance())
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_lastUpdate(0)
    , m_state(State::IDLE)
    , m_auditProgress(0.0f)
    , m_keysFound(0)
    , m_auditStartMs(0)
    , m_detailsScroll(0)
    , m_promptSelection(0)
    , m_readDataOption(false)
    , m_writeIncludeUid(false)
    , m_writeFromMenu(false)
    , m_writePromptSelection(0)
    , m_writeProgressDone(0)
    , m_writeProgressTotal(0) {
    memset(m_auditFilename, 0, sizeof(m_auditFilename));
}

RfidScreen::~RfidScreen() {
}

void RfidScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    m_state = State::IDLE;
    
    m_manager.activate();
    
    // Initialize audit with real hardware reader
    m_audit = std::make_unique<RfidAudit>(m_manager.getReader());
    
    footerHints_.setHints({
        {'\n', "Read", true}
    });
    footerHints_.setFocus(false);

    LOG_D("RFID", "RfidScreen visible");

    // Launched via the "RFID Write" menu entry: open straight into the restore picker instead
    // of the read dashboard (slice-0043). One-shot — cleared so a later plain open reads.
    if (s_writeModeRequested) {
        s_writeModeRequested = false;
        m_writeFromMenu = true;   // ESC from the picker returns to the RFID submenu, not here.
        enterWritePick();
    }
}

void RfidScreen::hide() {
    m_active = false;
    m_manager.deactivate();
    m_manager.clearTag();
}

void RfidScreen::update() {
    if (!m_active) return;

    if (m_state == State::IDLE || m_state == State::READING) {
        m_manager.update();
        if (m_manager.getLastTag()) {
            m_state = State::RESULT;
            m_needsRedraw = true;
            
            // Update hints: No more redundant Enter: Read. Add Details.
            bool canAudit = !(m_manager.getLastTag()->sak & 0x20);
            footerHints_.setHints({
                {'d', "Details", true},
                {'a', "Audit", canAudit}, // canAudit determines if it's enabled/gray
                {'s', "Save", hasAuditData()},
                {'w', "Write", true}
            });
        }
    }

    // Periodically force redraw for animations or time updates
    if (millis() - m_lastUpdate > 100) {
        m_lastUpdate = millis();
        m_needsRedraw = true;
    }
}

void RfidScreen::render(Canvas& canvas) {
    if (!m_needsRedraw) return;

    canvas.fillScreen(theme::BG_PRIMARY());
    
    // Standard status bar
    ui::StatusBar::render(canvas, "RFID", m_manager.isDetected() ? "READY" : "OFFLINE");
    
    if (!m_manager.isDetected()) {
        canvas.setTextColor(theme::ERROR());
        canvas.setCursor(config::SCREEN_WIDTH / 2 - 50, 60);
        canvas.print("NO NFC UNIT DETECTED");
    } else {
        switch (m_state) {
            case State::IDLE:
            case State::READING: {
                // Signal Pulse Animation
                int centerX = config::SCREEN_WIDTH / 2;
                int centerY = 75;
                uint32_t pulse = (millis() / 500) % 3;
                
                canvas.drawCircle(centerX, centerY, 15 + (pulse * 8), theme::ACCENT());
                canvas.drawCircle(centerX, centerY, 15 + (pulse * 8) + 1, theme::ACCENT());
                
                canvas.fillCircle(centerX, centerY, 10, theme::ACCENT());
                
                canvas.setTextColor(theme::TEXT_SECONDARY());
                canvas.setTextSize(1);
                const char* msg = (m_state == State::READING) ? "BRING TAG CLOSER..." : "READY TO READ";
                int msgW = strlen(msg) * 6;
                canvas.setCursor(centerX - (msgW / 2), centerY + 35);
                canvas.print(msg);
                break;
            }
                
            case State::RESULT:
                drawDashboard(canvas);
                break;
                
            case State::AUDIT_PROMPT:
                drawAuditPrompt(canvas);
                break;
                
            case State::AUDITING:
                drawAuditProgress(canvas);
                break;
                
            case State::AUDIT_SUMMARY:
                drawAuditSummary(canvas);
                break;
                
            case State::SECTOR_DETAIL:
                drawSectorDetail(canvas);
                break;
                
            case State::DETAILS:
                drawDetailsSubscreen(canvas);
                break;

            case State::WRITE_PICK:
                drawWritePick(canvas);
                break;

            case State::WRITE_PROMPT:
                drawWritePrompt(canvas);
                break;

            case State::WRITING:
                drawWriting(canvas);
                break;

            case State::WRITE_SUMMARY:
                drawWriteSummary(canvas);
                break;
        }
    }
    
    footerHints_.render(canvas);
    
    if (nameInput_.isVisible()) {
        nameInput_.render(canvas);
    }
    
    m_needsRedraw = false;
}

void RfidScreen::drawDashboard(Canvas& canvas) {
    const auto* tag = m_manager.getLastTag();
    if (!tag) return;
    
    // Result Card
    int cardX = 10;
    int cardY = config::STATUS_BAR_HEIGHT + 8;
    int cardW = config::SCREEN_WIDTH - 20;
    int cardH = 75;
    
    canvas.fillRoundRect(cardX, cardY, cardW, cardH, 4, theme::BG_SECONDARY());
    canvas.drawRoundRect(cardX, cardY, cardW, cardH, 4, theme::SUCCESS());

    // UID Large
    canvas.setTextColor(theme::SUCCESS());
    canvas.setTextSize(2);
    int uidW = tag->uid.length() * 12;
    canvas.setCursor(cardX + (cardW / 2) - (uidW / 2), cardY + 12);
    canvas.print(tag->uid.c_str());
    
    // Info Rows
    canvas.setTextSize(1);
    
    int rowY = cardY + 38;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(cardX + 15, rowY);
    canvas.print("TYPE: ");
    
    // Multi-row Type Handling to prevent overflow
    canvas.setTextColor(theme::TEXT_PRIMARY());
    std::string typeInfo = tag->type;
    if (typeInfo.length() > 20) {
        // Find a space to split if possible
        size_t split = typeInfo.find(' ', 15);
        if (split != std::string::npos && split < 25) {
            canvas.print(typeInfo.substr(0, split).c_str());
            rowY += 12;
            canvas.setCursor(cardX + 15 + 36, rowY); // Indent to match "TYPE: "
            canvas.print(typeInfo.substr(split + 1).c_str());
        } else {
            canvas.print(typeInfo.substr(0, 20).c_str());
            rowY += 12;
            canvas.setCursor(cardX + 15 + 36, rowY);
            canvas.print(typeInfo.substr(20).c_str());
        }
    } else {
        canvas.print(typeInfo.c_str());
    }
    
    rowY = cardY + cardH - 12; // Bottom-aligned SAK
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(cardX + 15, rowY);
    canvas.print("SAK : 0x");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(tag->sak, HEX);
    
    // Tag Badge
    canvas.fillRoundRect(cardX + cardW - 45, cardY + cardH - 18, 40, 12, 2, theme::SUCCESS());
    canvas.setTextColor(theme::BG_PRIMARY());
    canvas.setCursor(cardX + cardW - 42, cardY + cardH - 16);
    canvas.print("PICC");
}

void RfidScreen::drawAuditProgress(Canvas& canvas) {
    int centerX = config::SCREEN_WIDTH / 2;
    int y = config::STATUS_BAR_HEIGHT + 6;
    
    // Header
    canvas.setTextColor(theme::ACCENT());
    canvas.setTextSize(1);
    const char* title = "DICTIONARY AUDITING";
    canvas.setCursor(centerX - (strlen(title) * 3), y);
    canvas.print(title);
    
    y += 12;

    // Sector Grid (16 Sectors, 8x2)
    int gridX = 15;
    int gridY = y;
    int boxW = 25;
    int boxH = 18;
    int spacing = 2;

    for (int s = 0; s < 16; s++) {
        int col = s % 8;
        int row = s / 8;
        int bx = gridX + (col * (boxW + spacing));
        int by = gridY + (row * (boxH + spacing));

        bool isActive = (m_state == State::AUDITING && m_lastAuditProgress.currentSector == s);
        
        // Background box
        uint16_t boxColor = theme::BG_SECONDARY();
        if (isActive) boxColor = theme::ACCENT();
        
        canvas.fillRoundRect(bx, by, boxW, boxH, 2, boxColor);
        
        // Sector Number
        canvas.setTextColor(isActive ? theme::BG_PRIMARY() : theme::TEXT_DISABLED());
        canvas.setCursor(bx + 4, by + 4);
        canvas.printf("%02d", s);

        // Key Dots
        // Key A
        uint16_t colorA = theme::TEXT_DISABLED();
        if (m_lastAuditProgress.sectorResults[s][0]) colorA = theme::SUCCESS();
        else if (isActive && !m_lastAuditProgress.isKeyB) colorA = theme::WARNING();
        
        canvas.fillCircle(bx + 20, by + 6, 3, colorA);

        // Key B
        uint16_t colorB = theme::TEXT_DISABLED();
        if (m_lastAuditProgress.sectorResults[s][1]) colorB = theme::SUCCESS();
        else if (isActive && m_lastAuditProgress.isKeyB) colorB = theme::WARNING();
        
        canvas.fillCircle(bx + 20, by + 12, 3, colorB);
    }

    y += (boxH + spacing) * 2 + 10;

    // Progress Bar
    int barW = 180;
    int barH = 10;
    int x = centerX - (barW / 2);
    canvas.drawRoundRect(x, y, barW, barH, 2, theme::BG_SECONDARY());
    canvas.fillRoundRect(x + 2, y + 2, (barW - 4) * m_auditProgress, barH - 4, 1, theme::SUCCESS());
    
    y += 15;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.printf("KEYS: %d", m_keysFound);
    
    canvas.setCursor(x + barW - 30, y);
    canvas.printf("%d%%", (int)(m_auditProgress * 100));

    // Status Line
    y += 12;
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(centerX - 50, y);
    canvas.printf("Testing: Sector %02d %s", 
        m_lastAuditProgress.currentSector, 
        m_lastAuditProgress.isKeyB ? "B" : "A");

    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(centerX - 42, config::SCREEN_HEIGHT - 12);
    canvas.print("Press ESC to stop");
}

void RfidScreen::drawAuditPrompt(Canvas& canvas) {
    ui::StatusBar::render(canvas, "Audit Setup");
    
    const auto* tag = m_manager.getLastTag();
    if (!tag) return;
    
    int16_t y = config::STATUS_BAR_HEIGHT + 10;
    int16_t x = 8;
    int16_t screenW = config::SCREEN_WIDTH;
    const int16_t rowH = 13;
    
    // Target info (not selectable)
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("Target: ");
    canvas.setTextColor(theme::SUCCESS());
    canvas.print(tag->uid.c_str());
    y += rowH + 4;
    // The dump is named at save time (slice-0050), so no filename is shown on the pre-audit prompt.

    canvas.drawFastHLine(x, y, screenW - 16, theme::BG_SECONDARY());
    y += 6;
    
    // Option 0: Read Block Data (selectable)
    bool opt0Selected = (m_promptSelection == 0);
    if (opt0Selected) {
        canvas.fillRect(0, y - 1, screenW, rowH, theme::BG_SELECTED());
        canvas.setTextColor(theme::TEXT_PRIMARY());
    } else {
        canvas.setTextColor(theme::TEXT_SECONDARY());
    }
    canvas.setCursor(x, y);
    canvas.print("Read Block Data:");
    
    // Value aligned to right
    int16_t valX = screenW - 40;
    canvas.setCursor(valX, y);
    if (m_readDataOption) {
        if (!opt0Selected) canvas.setTextColor(theme::SUCCESS());
        canvas.print("[ON]");
    } else {
        if (!opt0Selected) canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.print("[OFF]");
    }
    y += rowH;
    
    // Description under the option
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(x + 6, y);
    if (m_readDataOption) {
        canvas.print("Saves block data to SD");
    } else {
        canvas.print("Keys only (faster)");
    }
    y += rowH + 4;
    
    // Separator
    canvas.drawFastHLine(x, y, screenW - 16, theme::BG_SECONDARY());
    y += 8;
    
    // Option 1: Start Audit button (selectable)
    bool opt1Selected = (m_promptSelection == 1);
    if (opt1Selected) {
        canvas.fillRect(0, y - 1, screenW, rowH, theme::SUCCESS());
        canvas.setTextColor(theme::TEXT_PRIMARY());
    } else {
        canvas.setTextColor(theme::SUCCESS());
    }
    canvas.setCursor(x, y);
    canvas.print(">> START AUDIT <<");
    y += rowH + 4;
    
    // Estimated time at bottom
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(x, y);
    canvas.print("Est. time: ");
    canvas.setTextColor(theme::WARNING());
    if (m_readDataOption) {
        canvas.print("~15-25s");
    } else {
        canvas.print("~8-12s");
    }
}

bool RfidScreen::handleInput(char key) {
    if (nameInput_.isVisible()) {
        // render() early-returns unless m_needsRedraw; the popup repaints only when we
        // mark the screen dirty, so every popup keystroke/cancel must request a redraw
        // or typing/backspace/ESC produce no visible change (looks frozen).
        bool handled = nameInput_.handleInput(key);
        m_needsRedraw = true;
        return handled;
    }

    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action) return handleInput(action);
        m_needsRedraw = true;
        return true;
    }

    if (key == '`') {
        if (m_state == State::SECTOR_DETAIL) {
            // Go back to summary from sector detail
            m_state = State::AUDIT_SUMMARY;
            footerHints_.setHints({
                {'\n', "View", true},
                {'s', "Save", hasAuditData()},
                {'`', "Back", true}
            });
            m_needsRedraw = true;
        } else if (m_state == State::AUDIT_SUMMARY) {
            // Go back to result from summary
            m_state = State::RESULT;
            bool canAudit = (m_manager.getLastTag() && !(m_manager.getLastTag()->sak & 0x20));
            footerHints_.setHints({
                {'d', "Details", true},
                {'a', "Audit", canAudit},
                {'s', "Save", hasAuditData()}
            });
            m_needsRedraw = true;
        } else if (m_state == State::AUDIT_PROMPT) {
            // Cancel pre-audit prompt
            m_state = State::RESULT;
            bool canAudit = (m_manager.getLastTag() && !(m_manager.getLastTag()->sak & 0x20));
            footerHints_.setHints({
                {'d', "Details", true},
                {'a', "Audit", canAudit},
                {'s', "Save", hasAuditData()}
            });
            m_needsRedraw = true;
        } else if (m_state == State::AUDITING || m_state == State::DETAILS) {
            m_state = State::RESULT;
            m_detailsScroll = 0;  // Reset scroll
            
            bool canAudit = (m_manager.getLastTag() && !(m_manager.getLastTag()->sak & 0x20));
            footerHints_.setHints({
                {'d', "Details", true},
                {'a', "Audit", canAudit},
                {'s', "Save", hasAuditData()}
            });
            m_needsRedraw = true;
        } else if (m_state == State::WRITE_PROMPT) {
            // Back to the file picker.
            enterWritePick();
        } else if (m_state == State::WRITE_PICK || m_state == State::WRITE_SUMMARY) {
            if (m_writeFromMenu) {
                // Launched from the RFID submenu: ESC returns there, not to the read screen.
                m_shouldExit = true;
            } else {
                m_state = State::IDLE;
                footerHints_.setHints({
                    {'\n', "Read", true}
                });
                m_needsRedraw = true;
            }
        } else if (m_state == State::RESULT) {
            m_manager.clearTag();
            m_state = State::IDLE;
            footerHints_.setHints({
                {'\n', "Read", true}
            });
            m_needsRedraw = true;
        } else {
            m_shouldExit = true;
        }
        return true;
    }

    // Action Dispatch
    if (key == '\n') {
        if (m_state == State::IDLE || m_state == State::RESULT) {
            m_state = State::READING;
            m_manager.clearTag();
            ToastManager::getInstance().show("Reading Tag...", ToastType::INFO);
            footerHints_.setHints({
                {'\n', "Stop", true}
            });
            m_needsRedraw = true;
            return true;
        } else if (m_state == State::READING) {
            m_state = State::IDLE;
            footerHints_.setHints({
                {'\n', "Read", true}
            });
            m_needsRedraw = true;
            return true;
        }
    }

    if (key == 'a' && (m_state == State::RESULT || m_state == State::IDLE)) {
        if (m_manager.getLastTag()) {
            if (m_manager.getLastTag()->sak & 0x20) {
                ToastManager::getInstance().show("Audit not supported for this type", ToastType::WARNING);
            } else {
                promptAudit();  // Show pre-audit prompt
            }
        } else {
            ToastManager::getInstance().show("Place tag first", ToastType::WARNING);
        }
        return true;
    }

    if (key == 'w' && (m_state == State::RESULT || m_state == State::IDLE)) {
        m_writeFromMenu = false;  // Entered from the dashboard; ESC returns to the reader.
        enterWritePick();
        return true;
    }

    // WRITE_PICK: navigate the .nfc file list, Enter loads the chosen dump.
    if (m_state == State::WRITE_PICK) {
        switch (key) {
            case ';':
                writeFileList_.setItemCount((int)m_writeFiles.size());
                writeFileList_.moveUp();
                m_needsRedraw = true;
                return true;
            case '.':
                writeFileList_.setItemCount((int)m_writeFiles.size());
                writeFileList_.moveDown();
                m_needsRedraw = true;
                return true;
            case '\n':
                if (!m_writeFiles.empty() &&
                    loadWriteDump(m_writeFiles[writeFileList_.selectedIndex()])) {
                    m_state = State::WRITE_PROMPT;
                    m_writePromptSelection = 0;
                    footerHints_.setHints({
                        {';', "^", true}, {'.', "v", true},
                        {'\n', "Select", true}, {'`', "Back", true}
                    });
                    m_needsRedraw = true;
                }
                return true;
        }
    }

    // WRITE_PROMPT: toggle the UID opt-in, or start the restore.
    if (m_state == State::WRITE_PROMPT) {
        switch (key) {
            case ';':
                if (m_writePromptSelection > 0) { m_writePromptSelection--; m_needsRedraw = true; }
                return true;
            case '.':
                if (m_writePromptSelection < 1) { m_writePromptSelection++; m_needsRedraw = true; }
                return true;
            case '\n':
                if (m_writePromptSelection == 0) {
                    m_writeIncludeUid = !m_writeIncludeUid;
                    m_needsRedraw = true;
                } else {
                    startWrite();
                }
                return true;
        }
    }

    // WRITE_SUMMARY: Done returns to wherever the flow was entered from.
    if (m_state == State::WRITE_SUMMARY && key == '\n') {
        if (m_writeFromMenu) {
            m_shouldExit = true;
        } else {
            m_state = State::IDLE;
            footerHints_.setHints({{'\n', "Read", true}});
            m_needsRedraw = true;
        }
        return true;
    }

    if (key == 'd' && m_state == State::RESULT) {
        m_state = State::DETAILS;
        m_detailsScroll = 0;  // Reset scroll on entry
        footerHints_.setHints({
            {';', "^", true},  // Scroll up
            {'.', "v", true},  // Scroll down
            {'`', "Back", true}
        });
        m_needsRedraw = true;
        return true;
    }

    if (m_state == State::RESULT) {
        if (key == 's') {
            // Save is greyed until an audit produces a dump; pressing it before then is a no-op
            // (the footer already shows it disabled) rather than a loud "Save Failed" (slice-0050).
            if (!hasAuditData()) return true;
            nameInput_.show("Save As", m_auditFilename, false, sizeof(m_auditFilename) - 1);
            nameInput_.setOnSubmit([this](const char* name) {
                const char* chosen = (name && name[0] != '\0') ? name : m_auditFilename;
                saveDump(chosen);
            });
            m_needsRedraw = true;
            return true;
        }
    }

    // AUDIT_SUMMARY state: navigate sectors and view details
    if (m_state == State::AUDIT_SUMMARY) {
        switch (key) {
            case ';':  // Up
                summaryList_.setItemCount(MIFARE_1K_SECTORS);
                summaryList_.moveUp();
                m_needsRedraw = true;
                return true;

            case '.':  // Down
                summaryList_.setItemCount(MIFARE_1K_SECTORS);
                summaryList_.moveDown();
                m_needsRedraw = true;
                return true;

            case '\n':  // Enter - view sector detail
                m_state = State::SECTOR_DETAIL;
                footerHints_.setHints({
                    {'`', "Back", true}
                });
                m_needsRedraw = true;
                return true;
                
            case 's':  // Save - prompt for the dump name, pre-filled with the UID (slice-0050)
                nameInput_.show("Save As", m_auditFilename, false,
                                sizeof(m_auditFilename) - 1);
                nameInput_.setOnSubmit([this](const char* name) {
                    // An empty entry falls back to the UID: never write a nameless .nfc.
                    const char* chosen = (name && name[0] != '\0') ? name : m_auditFilename;
                    saveDump(chosen);
                });
                m_needsRedraw = true;
                return true;
        }
    }

    // AUDIT_PROMPT state: navigate and select options
    if (m_state == State::AUDIT_PROMPT) {
        const int8_t maxItems = 2;  // 0=Read Data, 1=Start
        
        switch (key) {
            case ';':  // Up
                if (m_promptSelection > 0) {
                    m_promptSelection--;
                    m_needsRedraw = true;
                }
                return true;
                
            case '.':  // Down
                if (m_promptSelection < maxItems - 1) {
                    m_promptSelection++;
                    m_needsRedraw = true;
                }
                return true;
                
            case '\n':  // Enter - select current option
                if (m_promptSelection == 0) {
                    // Toggle read data option
                    m_readDataOption = !m_readDataOption;
                    m_needsRedraw = true;
                } else if (m_promptSelection == 1) {
                    // Start the audit directly: the dump is buffered in RAM and named at save time
                    // from the summary screen (slice-0050), not streamed to a pre-named file.
                    startAuditWithOptions(m_readDataOption);
                }
                return true;
        }
    }

    // DETAILS state: scrolling
    if (m_state == State::DETAILS) {
        int maxScroll = getDetailsContentHeight();
        
        switch (key) {
            case ';':  // Scroll up
                if (m_detailsScroll > 0) {
                    m_detailsScroll--;
                    m_needsRedraw = true;
                }
                return true;
                
            case '.':  // Scroll down
                if (m_detailsScroll < maxScroll) {
                    m_detailsScroll++;
                    m_needsRedraw = true;
                }
                return true;
        }
    }

    // SECTOR_DETAIL state: just allow back navigation (handled by ESC above)
    if (m_state == State::SECTOR_DETAIL) {
        // Navigation already handled by ESC
        return false;
    }

    return false;
}

void RfidScreen::promptAudit() {
    if (!m_manager.getLastTag()) return;
    
    // Set default filename from UID
    strncpy(m_auditFilename, m_manager.getLastTag()->uid.c_str(), sizeof(m_auditFilename) - 1);
    m_readDataOption = false;
    m_promptSelection = 0;  // Start on first option
    
    m_state = State::AUDIT_PROMPT;
    m_needsRedraw = true;
    
    footerHints_.setHints({
        {';', "^", true},
        {'.', "v", true},
        {'\n', "Select", true},
        {'`', "Back", true}
    });
}

void RfidScreen::startAuditWithOptions(bool readData) {
    if (!m_audit || !m_manager.getLastTag()) return;

    m_state = State::AUDITING;
    m_auditProgress = 0.0f;
    m_keysFound = 0;
    memset(&m_lastAuditProgress, 0, sizeof(m_lastAuditProgress));
    m_needsRedraw = true;

    // Set UI hints for auditing
    footerHints_.clear();

    // Force initial render BEFORE the blocking audit starts
    adversary_ui_render_forced();

    // The dump is buffered in RAM and written at save time (slice-0050), so no file is opened here.
    AuditOptions opts = {
        .readData = readData
    };

    // Run audit with options
    int result = m_audit->runDictionaryAttack([this](const AuditProgressInfo& info) {
        m_lastAuditProgress = info;
        m_auditProgress = info.overallProgress;
        m_keysFound = info.keysFound;
        m_needsRedraw = true;
        adversary_ui_render_forced(); 
        delay(1);
    }, opts);
    
    if (result == -1) {
        m_state = State::RESULT;
        m_needsRedraw = true;
        ToastManager::getInstance().show("Tag Type Not Supported", ToastType::ERROR);
        
        footerHints_.setHints({
            {'\n', "Read", true},
            {'a', "Audit", true},
            {'s', "Save", hasAuditData()}
        });
    } else {
        m_keysFound = result;
        onAuditComplete();
    }
}

void RfidScreen::startAudit() {
    // Legacy: now goes through promptAudit
    promptAudit();
}

void RfidScreen::onAuditComplete() {
    // Go to summary screen instead of result
    m_state = State::AUDIT_SUMMARY;
    summaryList_.setItemCount(MIFARE_1K_SECTORS);
    summaryList_.setSelectedIndex(0);
    m_needsRedraw = true;
    
    char msg[32];
    snprintf(msg, sizeof(msg), "Audit Done: %d keys", m_keysFound);
    ToastManager::getInstance().show(msg, ToastType::SUCCESS);
    
    footerHints_.setHints({
        {'\n', "View", true},
        {'s', "Save", hasAuditData()},
        {'`', "Back", true}
    });
}

bool RfidScreen::hasAuditData() const {
    // The dashboard Save writes the audit dump, which only exists after a run. Without it there is
    // nothing to save, so the Save affordance stays greyed (slice-0050).
    return m_audit && !m_audit->getResults().empty();
}

void RfidScreen::saveDump(const char* name) {
    if (m_audit && m_audit->saveToSD(name)) {
        ToastManager::getInstance().show("Saved to /RFID/", ToastType::SUCCESS);
    } else {
        ToastManager::getInstance().show("Save Failed", ToastType::ERROR);
    }
}

void RfidScreen::drawDetailsSubscreen(Canvas& canvas) {
    const auto* tag = m_manager.getLastTag();
    if (!tag) return;
    
    const int16_t contentStartY = config::STATUS_BAR_HEIGHT + 6;
    const int16_t contentHeight = config::SCREEN_HEIGHT - contentStartY - ui::FOOTER_HEIGHT;
    const int16_t lineHeight = 12;
    const int16_t scrollStep = lineHeight;
    
    // Calculate virtual y position with scroll offset
    int16_t virtualY = contentStartY - (m_detailsScroll * scrollStep);
    int x = 15;
    
    // Helper to check if a line is visible
    auto isVisible = [&](int16_t y) -> bool {
        return y >= contentStartY && y < (contentStartY + contentHeight);
    };
    
    // Helper to draw row with visibility check
    auto drawRow = [&](const char* label, const char* value, uint16_t color = theme::TEXT_PRIMARY()) {
        if (isVisible(virtualY)) {
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.setCursor(x, virtualY);
            canvas.print(label);
            canvas.setTextColor(color);
            canvas.print(value);
        }
        virtualY += lineHeight;
    };
    
    // Title row
    if (isVisible(virtualY)) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setTextSize(1);
        canvas.setCursor(x, virtualY);
        canvas.print("TAG DETAILS");
    }
    virtualY += 16;
    
    // Separator
    if (isVisible(virtualY)) {
        canvas.drawFastHLine(x, virtualY, config::SCREEN_WIDTH - 30, theme::BG_SECONDARY());
    }
    virtualY += 8;
    
    // UID
    drawRow("UID  : ", tag->uid.c_str(), theme::SUCCESS());
    
    // SAK
    char sakStr[16];
    snprintf(sakStr, sizeof(sakStr), "0x%02X", tag->sak);
    drawRow("SAK  : ", sakStr, theme::ACCENT());
    
    // Type
    drawRow("TYPE : ", tag->type.c_str(), theme::TEXT_PRIMARY());

    // Magic (Gen1a backdoor) — the single most useful fact for a clone workflow:
    // YES means block 0 / the UID is rewritable. Drawn in the warning accent so a
    // cloneable card is obvious at a glance.
    drawRow("MAGIC: ", tag->isMagic ? "YES (Gen1a)" : "NO",
            tag->isMagic ? theme::WARNING() : theme::TEXT_DISABLED());

    virtualY += 6;
    
    // Tag-specific info
    if (tag->sak & 0x20) {
        // ISO/IEC 14443-4 section
        if (isVisible(virtualY)) {
            canvas.setTextColor(theme::WARNING());
            canvas.setCursor(x, virtualY);
            canvas.print("[ISO/IEC 14443-4]");
        }
        virtualY += lineHeight + 4;
        
        if (!tag->ats.empty()) {
            drawRow("ATS  : ", "", theme::TEXT_SECONDARY());
            virtualY -= lineHeight;  // Back up
            
            std::string ats = tag->ats;
            // Split into chunks of 24 chars
            int pos = 0;
            while (pos < (int)ats.length()) {
                std::string chunk = ats.substr(pos, 24);
                if (isVisible(virtualY)) {
                    canvas.setTextColor(theme::TEXT_PRIMARY());
                    canvas.setCursor(x + (pos == 0 ? 36 : 36), virtualY);
                    canvas.print(chunk.c_str());
                }
                virtualY += lineHeight;
                pos += 24;
            }
        }
        
        virtualY += 6;
        drawRow("AUTH : ", "NOT SUPPORTED", theme::ERROR());
        
    } else {
        // MIFARE Classic section
        if (isVisible(virtualY)) {
            canvas.setTextColor(theme::SUCCESS());
            canvas.setCursor(x, virtualY);
            canvas.print("[MIFARE CLASSIC]");
        }
        virtualY += lineHeight + 4;
        
        drawRow("AUTH : ", "SUPPORTED", theme::SUCCESS());
        drawRow("MODE : ", "Dictionary Attack", theme::TEXT_PRIMARY());
        
        virtualY += 6;
        
        // Show audit results if available
        if (m_audit && !m_audit->getResults().empty()) {
            AuditSummary summary = m_audit->getSummary();
            
            if (isVisible(virtualY)) {
                canvas.drawFastHLine(x, virtualY, config::SCREEN_WIDTH - 30, theme::BG_SECONDARY());
            }
            virtualY += 8;
            
            if (isVisible(virtualY)) {
                canvas.setTextColor(theme::ACCENT());
                canvas.setCursor(x, virtualY);
                canvas.print("[LAST AUDIT]");
            }
            virtualY += lineHeight + 4;
            
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", summary.totalKeysFound);
            drawRow("KEYS : ", buf, summary.totalKeysFound > 0 ? theme::SUCCESS() : theme::ERROR());
            
            snprintf(buf, sizeof(buf), "%d/%d/%d", 
                summary.sectorsFullyCracked, 
                summary.sectorsPartiallyCracked, 
                summary.sectorsUncracked);
            drawRow("SECT : ", buf, theme::TEXT_PRIMARY());
            
            snprintf(buf, sizeof(buf), "%.1fs", summary.durationMs / 1000.0f);
            drawRow("TIME : ", buf, theme::TEXT_DISABLED());
        }
    }
    
    // Show scroll indicator if content overflows
    int totalLines = (virtualY - contentStartY + (m_detailsScroll * scrollStep)) / lineHeight;
    int maxScroll = (totalLines * lineHeight - contentHeight) / scrollStep;
    if (maxScroll > 0) {
        char scrollInfo[16];
        snprintf(scrollInfo, sizeof(scrollInfo), "%d/%d", m_detailsScroll + 1, maxScroll + 1);
        footerHints_.setRightContent(scrollInfo);
    }
}

int RfidScreen::getDetailsContentHeight() const {
    const auto* tag = m_manager.getLastTag();
    if (!tag) return 0;
    
    // Estimate content lines based on tag type
    int lines = 7;  // Base: title, separator, UID, SAK, TYPE, MAGIC, separator
    
    if (tag->sak & 0x20) {
        lines += 3;  // ISO header, ATS
        if (!tag->ats.empty()) {
            lines += (tag->ats.length() / 24) + 1;
        }
    } else {
        lines += 4;  // MIFARE header, AUTH, MODE
        if (m_audit && !m_audit->getResults().empty()) {
            lines += 6;  // Audit section
        }
    }
    
    return lines;
}

int RfidScreen::getVisibleSummaryItems() const {
    // Calculate based on available space
    int16_t headerHeight = config::STATUS_BAR_HEIGHT + 40;  // Title + stats
    int16_t availableHeight = config::SCREEN_HEIGHT - headerHeight - ui::FOOTER_HEIGHT;
    return availableHeight / theme::LIST_ITEM_HEIGHT;
}

void RfidScreen::drawSectorListItem(Canvas& canvas, int16_t y, int sector,
                                     const MifareSectorData& data) {
    // Selection is signalled by the component-drawn band (slice-0063); no ">" cursor here.
    int16_t x = 22;

    // Sector number
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(x, y + 4);
    canvas.printf("%02d:", sector);
    
    x += 24;
    
    // Key A status
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y + 4);
    canvas.print("A");
    canvas.setTextColor(data.keyAFound ? theme::SUCCESS() : theme::ERROR());
    canvas.print(data.keyAFound ? "+" : "-");
    
    x += 20;
    
    // Key B status
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y + 4);
    canvas.print("B");
    canvas.setTextColor(data.keyBFound ? theme::SUCCESS() : theme::ERROR());
    canvas.print(data.keyBFound ? "+" : "-");
    // The cracked key name for the selected sector lives on the sector-detail screen (Enter);
    // the summary row stays uncluttered now the band alone marks the selection (slice-0063).
}

void RfidScreen::drawAuditSummary(Canvas& canvas) {
    ui::StatusBar::render(canvas, "Audit Results");
    
    const auto& results = m_audit->getResults();
    AuditSummary summary = m_audit->getSummary();
    
    int16_t y = config::STATUS_BAR_HEIGHT + 8;
    int16_t x = 10;
    
    // Stats header
    canvas.setTextColor(theme::SUCCESS());
    canvas.setTextSize(1);
    canvas.setCursor(x, y);
    canvas.printf("%d Keys Found", summary.totalKeysFound);
    
    y += 12;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.printf("%d full, %d partial, %d locked", 
        summary.sectorsFullyCracked, 
        summary.sectorsPartiallyCracked,
        summary.sectorsUncracked);
    
    y += 12;
    canvas.setCursor(x, y);
    canvas.printf("Duration: %.1fs", summary.durationMs / 1000.0f);
    
    y += 16;
    canvas.drawFastHLine(x, y, config::SCREEN_WIDTH - 20, theme::BG_SECONDARY());
    y += 6;
    
    // Sector list. The shared component owns the scroll window, thumb, and selection band; this
    // lambda paints one sector row's key columns (slice-0063).
    int16_t listY = y;
    int visibleItems = getVisibleSummaryItems();

    summaryList_.setItemCount(MIFARE_1K_SECTORS);
    ScrollableList::render(
        canvas, summaryList_, 0, listY, config::SCREEN_WIDTH,
        visibleItems * theme::LIST_ITEM_HEIGHT, theme::LIST_ITEM_HEIGHT,
        [this, &results](Canvas& c, int sector, int16_t itemY, bool /*selected*/) {
            if ((size_t)sector < results.size()) {
                drawSectorListItem(c, itemY, sector, results[sector]);
            }
        });

    // Scroll indicator
    char rightContent[24];  // sized for the format's worst case
    snprintf(rightContent, sizeof(rightContent), "%d/%d",
             summaryList_.selectedIndex() + 1, MIFARE_1K_SECTORS);
    footerHints_.setRightContent(rightContent);
}

void RfidScreen::drawSectorDetail(Canvas& canvas) {
    const int selectedSector = summaryList_.selectedIndex();
    if ((size_t)selectedSector >= m_audit->getResults().size()) return;

    const auto& data = m_audit->getResults()[selectedSector];
    
    char title[16];
    snprintf(title, sizeof(title), "Sector %02d", data.sector);
    ui::StatusBar::render(canvas, title);
    
    int16_t y = config::STATUS_BAR_HEIGHT + 12;
    int16_t x = 15;
    
    // Key A
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(x, y);
    canvas.print("Key A: ");
    
    if (data.keyAFound) {
        canvas.setTextColor(theme::SUCCESS());
        for (int i = 0; i < 6; i++) {
            canvas.printf("%02X", data.keyA[i]);
        }
        canvas.print(" ");
        canvas.setTextColor(theme::SUCCESS());
        canvas.print("+");
        
        y += 14;
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(x, y);
        canvas.printf("Match: %s", RfidAudit::getKeyName(data.keyAIndex));
    } else {
        canvas.setTextColor(theme::ERROR());
        canvas.print("???????????? -");
    }
    
    y += 20;
    
    // Key B
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("Key B: ");
    
    if (data.keyBFound) {
        canvas.setTextColor(theme::SUCCESS());
        for (int i = 0; i < 6; i++) {
            canvas.printf("%02X", data.keyB[i]);
        }
        canvas.print(" ");
        canvas.setTextColor(theme::SUCCESS());
        canvas.print("+");
        
        y += 14;
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(x, y);
        canvas.printf("Match: %s", RfidAudit::getKeyName(data.keyBIndex));
    } else {
        canvas.setTextColor(theme::ERROR());
        canvas.print("???????????? -");
    }
    
    y += 24;
    
    // Summary for this sector
    canvas.drawFastHLine(x, y, config::SCREEN_WIDTH - 30, theme::BG_SECONDARY());
    y += 10;
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    
    if (data.keyAFound && data.keyBFound) {
        canvas.setTextColor(theme::SUCCESS());
        canvas.print("FULLY CRACKED");
    } else if (data.keyAFound || data.keyBFound) {
        canvas.setTextColor(theme::WARNING());
        canvas.print("PARTIALLY CRACKED");
    } else {
        canvas.setTextColor(theme::ERROR());
        canvas.print("LOCKED");
    }
}

// ---- Write / restore flow (slice-0043) ----

void RfidScreen::enterWritePick() {
    m_writeFiles.clear();
    writeFileList_.setItemCount(0);

    File dir = SD.open(RFID_CAPTURES_DIR);
    if (dir && dir.isDirectory()) {
        for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
            std::string name = entry.name();
            size_t slash = name.find_last_of('/');  // Some cores return a full path.
            if (slash != std::string::npos) name = name.substr(slash + 1);
            if (!entry.isDirectory() && name.size() > 4 &&
                name.compare(name.size() - 4, 4, ".nfc") == 0) {
                m_writeFiles.push_back(name);
            }
        }
    }
    if (dir) dir.close();

    m_state = State::WRITE_PICK;
    footerHints_.setHints({
        {';', "^", true}, {'.', "v", true},
        {'\n', "Select", true}, {'`', "Back", true}
    });
    m_needsRedraw = true;
}

bool RfidScreen::loadWriteDump(const std::string& filename) {
    std::string path = std::string(RFID_CAPTURES_DIR) + "/" + filename;
    File file = SD.open(path.c_str(), FILE_READ);
    if (!file) {
        ToastManager::getInstance().show("Open failed", ToastType::ERROR);
        return false;
    }
    std::string text;
    text.reserve(file.size());
    uint8_t buffer[256];
    while (file.available()) {
        int read = file.read(buffer, sizeof(buffer));
        if (read <= 0) break;
        text.append(reinterpret_cast<char*>(buffer), read);
    }
    file.close();

    m_writeDump = parseNfcDump(text);
    if (!m_writeDump.valid) {
        LOG_W("RFID", "Write load: %s invalid (%u B): %s", filename.c_str(),
              (unsigned)text.size(), m_writeDump.parseError.c_str());
        ToastManager::getInstance().show("Bad .nfc file", ToastType::ERROR);
        return false;
    }
    m_writePlan = buildWritePlan(m_writeDump);
    m_writeIncludeUid = false;
    LOG_D("RFID", "Write load: %s %u B, %u blocks, %u writable, uid=%d",
          filename.c_str(), (unsigned)text.size(),
          (unsigned)m_writeDump.blocks.size(),
          (unsigned)m_writePlan.dataWrites.size(), m_writePlan.hasManufacturer);
    if (m_writePlan.dataWrites.empty() && !m_writePlan.hasManufacturer) {
        // A keys-only dump (audited with Read Block Data off), or one whose blocks all read back
        // as "??". Nothing to restore — report rather than write an empty card.
        ToastManager::getInstance().show("No block data in dump", ToastType::WARNING);
        return false;
    }
    return true;
}

void RfidScreen::startWrite() {
    if (!m_manager.getReader()) {
        ToastManager::getInstance().show("No reader", ToastType::ERROR);
        return;
    }

    m_state = State::WRITING;
    m_writeProgressDone = 0;
    m_writeProgressTotal = static_cast<int>(m_writePlan.dataWrites.size()) +
        (m_writeIncludeUid && m_writePlan.hasManufacturer ? 1 : 0);
    footerHints_.clear();
    m_needsRedraw = true;
    adversary_ui_render_forced();  // Show the progress screen before the blocking restore.

    RfidWriter writer(m_manager.getReader());
    m_writeResult = writer.restore(m_writePlan, m_writeDump, m_writeIncludeUid,
        [this](const WriteProgress& progress) {
            m_writeProgressDone = progress.done;
            m_writeProgressTotal = progress.total;
            m_needsRedraw = true;
            adversary_ui_render_forced();
            delay(1);
        });

    m_state = State::WRITE_SUMMARY;
    m_needsRedraw = true;

    char msg[40];
    snprintf(msg, sizeof(msg), "Wrote %d/%d blocks",
             m_writeResult.blocksWritten, m_writeResult.totalPlanned);
    ToastManager::getInstance().show(
        msg, m_writeResult.blocksWritten > 0 ? ToastType::SUCCESS : ToastType::ERROR);
    footerHints_.setHints({{'\n', "Done", true}, {'`', "Back", true}});
}

void RfidScreen::drawWritePick(Canvas& canvas) {
    ui::StatusBar::render(canvas, "Restore Dump");

    const int16_t x = 10;
    const int16_t screenW = config::SCREEN_WIDTH;
    canvas.setTextSize(1);

    if (m_writeFiles.empty()) {
        int16_t y = config::STATUS_BAR_HEIGHT + 8;
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(x, y);
        canvas.print("No .nfc dumps found.");
        y += 14;
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(x, y);
        canvas.print("Audit a card with");
        y += 12;
        canvas.setCursor(x, y);
        canvas.print("Read Block Data ON first.");
        return;
    }

    // List starts right under the status bar; the shared component shows WRITE_PICK_VISIBLE_ROWS
    // rows at a time, scrolls, and draws the thumb when the dumps overflow the window (this
    // replaced the hand-rolled up/down scroll triangles).
    const int16_t listTop = config::STATUS_BAR_HEIGHT + 2;
    const int total = static_cast<int>(m_writeFiles.size());

    writeFileList_.setItemCount(total);
    ScrollableList::render(
        canvas, writeFileList_, 0, listTop, screenW,
        WRITE_PICK_VISIBLE_ROWS * theme::LIST_ITEM_HEIGHT, theme::LIST_ITEM_HEIGHT,
        [this, x](Canvas& c, int idx, int16_t itemY, bool /*selected*/) {
            // Band drawn by the component; filename is content-coloured (slice-0063). This is a
            // file picker (a data list), so it uses the band-only standard, not the config ACCENT.
            c.setTextColor(theme::TEXT_PRIMARY());
            c.setCursor(x, itemY + 6);
            c.print(m_writeFiles[idx].c_str());
        });

    // char[8] truncated the "N/M" counter once the file count reached 4 digits
    // (e.g. "1000/1000" -> "1000/10"). Sized for the worst case of two 32-bit %d plus
    // '/' and NUL (24); interim until the shared count formatter lands (see issue #54).
    char rightContent[24];
    snprintf(rightContent, sizeof(rightContent), "%d/%d",
             writeFileList_.selectedIndex() + 1, total);
    footerHints_.setRightContent(rightContent);
}

void RfidScreen::drawWritePrompt(Canvas& canvas) {
    ui::StatusBar::render(canvas, "Write Plan");

    int16_t x = 8;
    int16_t y = config::STATUS_BAR_HEIGHT + 8;
    const int16_t rowH = 13;
    int16_t screenW = config::SCREEN_WIDTH;

    // Source UID (not selectable).
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("UID : ");
    canvas.setTextColor(theme::SUCCESS());
    canvas.print(m_writeDump.uid.c_str());
    y += rowH;

    // Plan summary.
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(x, y);
    canvas.printf("Data blocks : %d", (int)m_writePlan.dataWrites.size());
    y += rowH;
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(x, y);
    canvas.printf("Skipped: %d trailer, %d unread", m_writePlan.skippedTrailer,
                  m_writePlan.skippedUnknown);
    y += rowH + 2;

    canvas.drawFastHLine(x, y, screenW - 16, theme::BG_SECONDARY());
    y += 6;

    // Option 0: UID (block 0) opt-in.
    bool uidRowSelected = (m_writePromptSelection == 0);
    if (uidRowSelected) {
        canvas.fillRect(0, y - 1, screenW, rowH, theme::BG_SELECTED());
        canvas.setTextColor(theme::TEXT_PRIMARY());
    } else {
        canvas.setTextColor(theme::TEXT_SECONDARY());
    }
    canvas.setCursor(x, y);
    canvas.print("Write UID (magic):");
    canvas.setCursor(screenW - 40, y);
    if (m_writeIncludeUid) {
        if (!uidRowSelected) canvas.setTextColor(theme::WARNING());
        canvas.print("[ON]");
    } else {
        if (!uidRowSelected) canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.print("[OFF]");
    }
    y += rowH;
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(x + 6, y);
    canvas.print("Block 0; fails on normal cards");
    y += rowH + 4;

    // Option 1: start.
    bool startSelected = (m_writePromptSelection == 1);
    if (startSelected) {
        canvas.fillRect(0, y - 1, screenW, rowH, theme::SUCCESS());
        canvas.setTextColor(theme::TEXT_PRIMARY());
    } else {
        canvas.setTextColor(theme::SUCCESS());
    }
    canvas.setCursor(x, y);
    canvas.print(">> START WRITE <<");
}

void RfidScreen::drawWriting(Canvas& canvas) {
    int centerX = config::SCREEN_WIDTH / 2;
    int y = config::STATUS_BAR_HEIGHT + 20;

    canvas.setTextColor(theme::ACCENT());
    canvas.setTextSize(1);
    const char* title = "WRITING TO CARD";
    canvas.setCursor(centerX - (strlen(title) * 3), y);
    canvas.print(title);
    y += 20;

    float progress = m_writeProgressTotal > 0
                         ? (float)m_writeProgressDone / (float)m_writeProgressTotal
                         : 0.0f;
    int barW = 180;
    int barH = 12;
    int x = centerX - (barW / 2);
    canvas.drawRoundRect(x, y, barW, barH, 2, theme::BG_SECONDARY());
    canvas.fillRoundRect(x + 2, y + 2, (int)((barW - 4) * progress), barH - 4, 1,
                         theme::SUCCESS());
    y += barH + 10;

    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(centerX - 30, y);
    canvas.printf("%d / %d", m_writeProgressDone, m_writeProgressTotal);
}

void RfidScreen::drawWriteSummary(Canvas& canvas) {
    ui::StatusBar::render(canvas, "Write Result");

    int16_t x = 10;
    int16_t y = config::STATUS_BAR_HEIGHT + 10;
    const int16_t rowH = 14;

    if (!m_writeResult.cardSelected) {
        canvas.setTextColor(theme::ERROR());
        canvas.setCursor(x, y);
        canvas.print("No card detected.");
        return;
    }

    canvas.setTextColor(theme::SUCCESS());
    canvas.setTextSize(1);
    canvas.setCursor(x, y);
    canvas.printf("Written: %d / %d", m_writeResult.blocksWritten, m_writeResult.totalPlanned);
    y += rowH;

    if (m_writeResult.failedAuth > 0 || m_writeResult.failedWrite > 0) {
        canvas.setTextColor(theme::WARNING());
        canvas.setCursor(x, y);
        canvas.printf("Failed: %d auth, %d write", m_writeResult.failedAuth,
                      m_writeResult.failedWrite);
        y += rowH;
    }

    if (m_writeResult.uidAttempted) {
        y += 4;
        canvas.drawFastHLine(x, y, config::SCREEN_WIDTH - 20, theme::BG_SECONDARY());
        y += 6;
        canvas.setTextColor(m_writeResult.uidWritten ? theme::WARNING() : theme::TEXT_DISABLED());
        // Wrap the verdict string across the narrow screen.
        const std::string& line = m_writeResult.uidResult;
        size_t pos = 0;
        const size_t perLine = 30;
        while (pos < line.size()) {
            canvas.setCursor(x, y);
            canvas.print(line.substr(pos, perLine).c_str());
            pos += perLine;
            y += 12;
        }
    }
}

} // namespace adversary
