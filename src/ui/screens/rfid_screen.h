/**
 * @file rfid_screen.h
 * @brief Screen for RFID tag interaction
 */

#pragma once

#include "ui/screens/screen_interface.h"
#include "ui/components/footer_hints.h"
#include "modules/rfid/rfid_manager.h"
#include "modules/rfid/rfid_audit.h"
#include "ui/components/text_input_popup.h"

namespace adversary {

class RfidScreen : public IScreen {
public:
    RfidScreen();
    virtual ~RfidScreen();

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;
    const char* getName() const override { return "RFID"; }
    ScreenId getId() const override { return ScreenId::RFID; }
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }

private:
    RFIDManager& m_manager;
    std::unique_ptr<RfidAudit> m_audit;
    
    bool m_active;
    bool m_shouldExit;
    bool m_needsRedraw;
    uint32_t m_lastUpdate;
    
    // UI Components
    ui::FooterHints footerHints_;
    TextInputPopup nameInput_;
    
    // State
    enum class State {
        IDLE,
        READING,
        RESULT,
        AUDIT_PROMPT,   // Pre-audit filename/options prompt
        AUDITING,
        AUDIT_SUMMARY,  // Post-audit summary view
        SECTOR_DETAIL,  // Detailed sector view with keys
        DETAILS
    };
    State m_state;
    AuditProgressInfo m_lastAuditProgress;
    float m_auditProgress;
    int m_keysFound;
    
    // Audit summary navigation
    int8_t m_summaryScroll;      // Scroll position in summary list
    int8_t m_summarySelection;   // Selected sector in summary
    uint32_t m_auditStartMs;     // For elapsed time tracking
    
    // Details screen scrolling
    int8_t m_detailsScroll;      // Scroll position in details screen
    
    // Pre-audit options
    int8_t m_promptSelection;     // Selected option in audit prompt (0=readData, 1=start)
    bool m_readDataOption;        // Whether to read block data
    char m_auditFilename[64];     // Filename for audit results
    
    // Drawing methods
    void drawDashboard(Canvas& canvas);
    void drawAuditProgress(Canvas& canvas);
    void drawAuditPrompt(Canvas& canvas);
    void drawAuditSummary(Canvas& canvas);
    void drawSectorDetail(Canvas& canvas);
    void drawDetailsSubscreen(Canvas& canvas);
    
    // Helper methods
    void drawSectorListItem(Canvas& canvas, int16_t y, int sector, 
                            const MifareSectorData& data, bool selected);
    int getVisibleSummaryItems() const;
    int getDetailsContentHeight() const;
    
    // Actions
    void promptAudit();
    void startAuditWithOptions(const char* filename, bool readData);
    void startAudit();  // Legacy, now calls promptAudit
    void onAuditComplete();
    void saveDump(const char* name);
};

} // namespace adversary
