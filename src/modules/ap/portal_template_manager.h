/**
 * @file portal_template_manager.h
 * @brief Manages captive portal templates from SD card
 * 
 * Scans /adversary/portals/ for template folders containing index.html
 * and optional CSS/image assets. Templates can be loaded dynamically.
 */

#pragma once

#include <vector>
#include <cstring>
#include <cstdint>

namespace ap {

/**
 * @brief Portal template metadata
 */
struct PortalTemplate {
    char name[32] = {0};           // Display name (folder name or from manifest)
    char path[64] = {0};           // Full path to template folder
    bool hasCSS = false;           // Has style.css
    bool hasLogo = false;          // Has logo.png
    bool isBuiltIn = false;        // Built-in template (not from SD)
    
    void setName(const char* n) {
        strncpy(name, n, sizeof(name) - 1);
    }
    
    void setPath(const char* p) {
        strncpy(path, p, sizeof(path) - 1);
    }
};

/**
 * @brief Manager for captive portal templates from SD card
 * 
 * Usage:
 *   auto& mgr = PortalTemplateManager::getInstance();
 *   mgr.scanTemplates();  // Call at boot
 *   auto templates = mgr.getTemplateList();
 *   String html = mgr.loadTemplateHTML("google");
 */
class PortalTemplateManager {
public:
    static constexpr const char* PORTALS_DIR = "/adversary/portals";
    static constexpr size_t MAX_TEMPLATES = 16;
    
    /**
     * @brief Get singleton instance
     */
    static PortalTemplateManager& getInstance() {
        static PortalTemplateManager instance;
        return instance;
    }
    
    /**
     * @brief Scan SD card for portal templates
     * @return Number of templates found
     * 
     * Scans /adversary/portals/ for subdirectories containing index.html
     */
    size_t scanTemplates();
    
    /**
     * @brief Get list of available templates
     */
    const std::vector<PortalTemplate>& getTemplateList() const {
        return templates_;
    }
    
    /**
     * @brief Get template count
     */
    size_t getTemplateCount() const {
        return templates_.size();
    }
    
    /**
     * @brief Get template by index
     */
    const PortalTemplate* getTemplate(size_t index) const {
        if (index < templates_.size()) {
            return &templates_[index];
        }
        return nullptr;
    }
    
    /**
     * @brief Get template by name
     */
    const PortalTemplate* getTemplateByName(const char* name) const {
        for (const auto& t : templates_) {
            if (strcmp(t.name, name) == 0) {
                return &t;
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Load template HTML content from SD
     * @param name Template name (folder name)
     * @return HTML content or empty string if not found
     */
    bool loadTemplateHTML(const char* name, char* buffer, size_t bufferSize);
    
    /**
     * @brief Load template asset (CSS, image) from SD
     * @param templateName Template folder name
     * @param assetName Asset filename (e.g., "style.css", "logo.png")
     * @param buffer Output buffer
     * @param bufferSize Buffer size
     * @return Number of bytes read, 0 on failure
     */
    size_t loadTemplateAsset(const char* templateName, const char* assetName, 
                             uint8_t* buffer, size_t bufferSize);
    
    /**
     * @brief Check if templates have been scanned
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * @brief Clear template cache and rescan
     */
    void refresh() {
        templates_.clear();
        initialized_ = false;
        scanTemplates();
    }

private:
    PortalTemplateManager() = default;
    PortalTemplateManager(const PortalTemplateManager&) = delete;
    PortalTemplateManager& operator=(const PortalTemplateManager&) = delete;
    
    std::vector<PortalTemplate> templates_;
    bool initialized_ = false;
    
    /**
     * @brief Add built-in templates to list
     */
    void addBuiltInTemplates();
    
    /**
     * @brief Check if folder contains valid template (has index.html)
     */
    bool isValidTemplate(const char* folderPath);
};

} // namespace ap
