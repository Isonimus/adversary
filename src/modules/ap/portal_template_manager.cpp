/**
 * @file portal_template_manager.cpp
 * @brief Implementation of PortalTemplateManager for SD card templates
 */

#include "portal_template_manager.h"

#ifndef UNIT_TEST
#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#endif

namespace ap {

#ifndef UNIT_TEST

size_t PortalTemplateManager::scanTemplates() {
    templates_.clear();
    
    // Add built-in templates first
    addBuiltInTemplates();
    
    // Check if SD card is available
    if (!SD.begin()) {
        initialized_ = true;
        return templates_.size();
    }
    
    // Check if portals directory exists
    File portalsDir = SD.open(PORTALS_DIR);
    if (!portalsDir || !portalsDir.isDirectory()) {
        portalsDir.close();
        initialized_ = true;
        return templates_.size();
    }
    
    // Scan for subdirectories with index.html
    File entry;
    while ((entry = portalsDir.openNextFile()) && templates_.size() < MAX_TEMPLATES) {
        if (entry.isDirectory()) {
            char folderPath[64];
            snprintf(folderPath, sizeof(folderPath), "%s/%s", PORTALS_DIR, entry.name());
            
            // Skip if it's a built-in template name (already added)
            bool isBuiltIn = false;
            for (const auto& t : templates_) {
                if (t.isBuiltIn && strcmp(t.name, entry.name()) == 0) {
                    isBuiltIn = true;
                    break;
                }
            }
            
            if (!isBuiltIn && isValidTemplate(folderPath)) {
                PortalTemplate tmpl;
                tmpl.setName(entry.name());
                tmpl.setPath(folderPath);
                tmpl.isBuiltIn = false;
                
                // Check for optional assets
                char assetPath[80];
                snprintf(assetPath, sizeof(assetPath), "%s/style.css", folderPath);
                tmpl.hasCSS = SD.exists(assetPath);
                
                snprintf(assetPath, sizeof(assetPath), "%s/logo.png", folderPath);
                tmpl.hasLogo = SD.exists(assetPath);
                
                templates_.push_back(tmpl);
            }
        }
        entry.close();
    }
    
    portalsDir.close();
    initialized_ = true;
    return templates_.size();
}

bool PortalTemplateManager::isValidTemplate(const char* folderPath) {
    char indexPath[80];
    snprintf(indexPath, sizeof(indexPath), "%s/index.html", folderPath);
    return SD.exists(indexPath);
}

bool PortalTemplateManager::loadTemplateHTML(const char* name, char* buffer, size_t bufferSize) {
    const PortalTemplate* tmpl = getTemplateByName(name);
    if (!tmpl || tmpl->isBuiltIn) {
        return false;  // Built-in templates are handled differently
    }
    
    char indexPath[80];
    snprintf(indexPath, sizeof(indexPath), "%s/index.html", tmpl->path);
    
    File file = SD.open(indexPath, FILE_READ);
    if (!file) {
        return false;
    }
    
    size_t bytesRead = file.readBytes(buffer, bufferSize - 1);
    buffer[bytesRead] = '\0';
    file.close();
    
    return bytesRead > 0;
}

size_t PortalTemplateManager::loadTemplateAsset(const char* templateName, const char* assetName,
                                                 uint8_t* buffer, size_t bufferSize) {
    const PortalTemplate* tmpl = getTemplateByName(templateName);
    if (!tmpl || tmpl->isBuiltIn) {
        return 0;
    }
    
    char assetPath[96];
    snprintf(assetPath, sizeof(assetPath), "%s/%s", tmpl->path, assetName);
    
    File file = SD.open(assetPath, FILE_READ);
    if (!file) {
        return 0;
    }
    
    size_t bytesRead = file.read(buffer, bufferSize);
    file.close();
    
    return bytesRead;
}

void PortalTemplateManager::addBuiltInTemplates() {
    // Generic template (default)
    PortalTemplate generic;
    generic.setName("Generic");
    generic.isBuiltIn = true;
    templates_.push_back(generic);
    
    // Google template
    PortalTemplate google;
    google.setName("Google");
    google.isBuiltIn = true;
    templates_.push_back(google);
}

#else // UNIT_TEST

// Mock implementations for unit testing
size_t PortalTemplateManager::scanTemplates() {
    templates_.clear();
    addBuiltInTemplates();
    initialized_ = true;
    return templates_.size();
}

bool PortalTemplateManager::isValidTemplate(const char* folderPath) {
    (void)folderPath;
    return false;
}

bool PortalTemplateManager::loadTemplateHTML(const char* name, char* buffer, size_t bufferSize) {
    (void)name;
    (void)buffer;
    (void)bufferSize;
    return false;
}

size_t PortalTemplateManager::loadTemplateAsset(const char* templateName, const char* assetName,
                                                 uint8_t* buffer, size_t bufferSize) {
    (void)templateName;
    (void)assetName;
    (void)buffer;
    (void)bufferSize;
    return 0;
}

void PortalTemplateManager::addBuiltInTemplates() {
    PortalTemplate generic;
    generic.setName("Generic");
    generic.isBuiltIn = true;
    templates_.push_back(generic);
    
    PortalTemplate google;
    google.setName("Google");
    google.isBuiltIn = true;
    templates_.push_back(google);
}

#endif // UNIT_TEST

} // namespace ap
