/**
 * @file rfid_screen.cpp
 * @brief RfidScreen implementation
 */

#include "rfid_screen.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/toast_manager.h"
#include <SD.h>

extern void adversary_ui_render_forced();

namespace adversary {

RfidScreen::RfidScreen()
    : m_manager(RFIDManager::getInstance())
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_lastUpdate(0)
    , m_state(State::IDLE)
    , m_auditProgress(0.0f)
    , m_keysFound(0)
    , m_summaryScroll(0)
    , m_summarySelection(0)
    , m_auditStartMs(0)
    , m_detailsScroll(0)
    , m_promptSelection(0)
    , m_readDataOption(false) {
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
    
    Serial.println("[UI] RfidScreen visible");
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
                {'s', "Save", true}
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
    y += rowH;
    
    // Filename display (not selectable)
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("File  : ");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(m_auditFilename);
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.print(".nfc");
    y += rowH + 4;
    
    canvas.drawFastHLine(x, y, screenW - 16, theme::BG_SECONDARY());
    y += 6;
    
    // Option 0: Read Block Data (selectable)
    bool opt0Selected = (m_promptSelection == 0);
    if (opt0Selected) {
        canvas.fillRect(0, y - 1, screenW, rowH, theme::ACCENT());
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
        return nameInput_.handleInput(key);
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
                {'s', "Save", true},
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
                {'s', "Save", true}
            });
            m_needsRedraw = true;
        } else if (m_state == State::AUDIT_PROMPT) {
            // Cancel pre-audit prompt
            m_state = State::RESULT;
            bool canAudit = (m_manager.getLastTag() && !(m_manager.getLastTag()->sak & 0x20));
            footerHints_.setHints({
                {'d', "Details", true},
                {'a', "Audit", canAudit},
                {'s', "Save", true}
            });
            m_needsRedraw = true;
        } else if (m_state == State::AUDITING || m_state == State::DETAILS) {
            m_state = State::RESULT;
            m_detailsScroll = 0;  // Reset scroll
            
            bool canAudit = (m_manager.getLastTag() && !(m_manager.getLastTag()->sak & 0x20));
            footerHints_.setHints({
                {'d', "Details", true},
                {'a', "Audit", canAudit},
                {'s', "Save", true}
            });
            m_needsRedraw = true;
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
            nameInput_.show("Save Dump", "TagDump");
            nameInput_.setOnSubmit([this](const char* name) {
                saveDump(name);
            });
            return true;
        }
    }

    // AUDIT_SUMMARY state: navigate sectors and view details
    if (m_state == State::AUDIT_SUMMARY) {
        switch (key) {
            case ';':  // Up
                if (m_summarySelection > 0) {
                    m_summarySelection--;
                    if (m_summarySelection < m_summaryScroll) {
                        m_summaryScroll--;
                    }
                    m_needsRedraw = true;
                }
                return true;
                
            case '.':  // Down
                if (m_summarySelection < 15) {
                    m_summarySelection++;
                    int visible = getVisibleSummaryItems();
                    if (m_summarySelection >= m_summaryScroll + visible) {
                        m_summaryScroll++;
                    }
                    m_needsRedraw = true;
                }
                return true;
                
            case '\n':  // Enter - view sector detail
                m_state = State::SECTOR_DETAIL;
                footerHints_.setHints({
                    {'`', "Back", true}
                });
                m_needsRedraw = true;
                return true;
                
            case 's':  // Save
                saveDump("audit_results");
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
                    // Start audit
                    startAuditWithOptions(m_auditFilename, m_readDataOption);
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

void RfidScreen::startAuditWithOptions(const char* filename, bool readData) {
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

    // Build full path for dump
    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "/adversary/captures/RFID/%s.nfc", filename);
    
    // Ensure directory exists
    if (!SD.exists("/adversary/captures/RFID")) {
        SD.mkdir("/adversary/captures");
        SD.mkdir("/adversary/captures/RFID");
    }
    
    AuditOptions opts = {
        .readData = readData,
        .dumpPath = fullPath
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
            {'s', "Save", true}
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
    m_summaryScroll = 0;
    m_summarySelection = 0;
    m_needsRedraw = true;
    
    char msg[32];
    snprintf(msg, sizeof(msg), "Audit Done: %d keys", m_keysFound);
    ToastManager::getInstance().show(msg, ToastType::SUCCESS);
    
    footerHints_.setHints({
        {'\n', "View", true},
        {'s', "Save", true},
        {'`', "Back", true}
    });
}

void RfidScreen::saveDump(const char* name) {
    if (m_audit && m_audit->saveToSD(name)) {
        ToastManager::getInstance().show("Saved to /rfid/", ToastType::SUCCESS);
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
    int lines = 6;  // Base: title, separator, UID, SAK, TYPE, separator
    
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
                                     const MifareSectorData& data, bool selected) {
    int16_t x = 10;
    
    // Selection indicator
    if (selected) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(x, y + 4);
        canvas.print(">");
    }
    
    x += 12;
    
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
    
    // If a key was found, show which one
    if (data.keyAFound && selected) {
        x += 25;
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(x, y + 4);
        canvas.print(RfidAudit::getKeyName(data.keyAIndex));
    }
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
    
    // Sector list
    int16_t listY = y;
    int visibleItems = getVisibleSummaryItems();
    
    for (int i = 0; i < visibleItems && (m_summaryScroll + i) < 16; i++) {
        int sector = m_summaryScroll + i;
        int16_t itemY = listY + (i * theme::LIST_ITEM_HEIGHT);
        bool selected = (sector == m_summarySelection);
        
        if (selected) {
            canvas.fillRect(4, itemY, config::SCREEN_WIDTH - 8, theme::LIST_ITEM_HEIGHT, theme::BG_SECONDARY());
        }
        
        if ((size_t)sector < results.size()) {
            drawSectorListItem(canvas, itemY, sector, results[sector], selected);
        }
    }
    
    // Scroll indicator
    char rightContent[8];
    snprintf(rightContent, sizeof(rightContent), "%d/16", m_summarySelection + 1);
    footerHints_.setRightContent(rightContent);
}

void RfidScreen::drawSectorDetail(Canvas& canvas) {
    if ((size_t)m_summarySelection >= m_audit->getResults().size()) return;
    
    const auto& data = m_audit->getResults()[m_summarySelection];
    
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

} // namespace adversary
