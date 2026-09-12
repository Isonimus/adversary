/**
 * @file sd_manager.cpp
 * @brief SD Card management implementation
 * 
 * Uses Arduino SD library with proper SPI configuration for M5Stack Cardputer.
 */

#include "sd_manager.h"
#include "sd_mount_policy.h"
#include "config/config.h"

#ifndef UNIT_TEST
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include "config/pins.h"

#if defined(TARGET_CARDPUTER)
#include <M5Cardputer.h>
#elif defined(TARGET_M5STICK)
#include <M5StickCPlus2.h>
#endif

#endif

#include <cstring>
#include <cstdio>

namespace adversary {

#ifndef UNIT_TEST
namespace {
// The dedicated FSPI bus SDManager mounts the card on (Method 2). Shared with
// the multi-radio cap probe so both SPI devices drive one peripheral instance
// rather than a conflicting second SPIClass (slice-0002). Valid once Method 2
// has called sdSPI.begin(): this tracks bus initialisation, NOT card-mount
// success, so a cap seated with no card is still probeable. A launcher
// pre-mount (Method 1) returns before begin() and leaves the bus unowned.
SPIClass sdSPI(FSPI);
bool g_sdBusBegun = false;
}  // namespace
#endif

const char* sdStatusToString(SDStatus status) {
    switch (status) {
        case SDStatus::NOT_INITIALIZED: return "NOT_INITIALIZED";
        case SDStatus::NO_CARD:         return "NO_CARD";
        case SDStatus::MOUNT_FAILED:    return "MOUNT_FAILED";
        case SDStatus::READY:           return "READY";
        case SDStatus::ERROR:           return "ERROR";
        default:                        return "UNKNOWN";
    }
}

SDManager& SDManager::getInstance() {
    static SDManager instance;
    return instance;
}

SDManager::SDManager()
    : m_status(SDStatus::NOT_INITIALIZED)
    , m_initialized(false)
    , m_noCardVerdict(false)
{
}

SDManager::~SDManager() {
    deinit();
}

#ifndef UNIT_TEST

bool SDManager::init() {
    if (m_initialized) {
        return true;
    }

#if defined(TARGET_M5STICK)
    // M5StickC Plus 2 has no SD card slot - skip SD init entirely
    Serial.println("[SDManager] M5Stick has no SD slot - using internal storage");
    m_status = SDStatus::NO_CARD;
    m_initialized = false;
    m_noCardVerdict = true;  // never any card here; keep remount() a no-op
    return false;  // Not an error, just no SD available
#endif

    Serial.println("[SDManager] Initializing SD card...");

    // De-select any SPI peripheral sharing the bus before (re)mounting the SD.
    // A floating chip-select drives MISO and corrupts SD init. The same header
    // hosts either the LoRa cap (NSS on G5) or the CC1101/NRF24 cap (CC1101 CS
    // on G15, NRF24 CS on G4); pull-ups on the latter hold a floating CS
    // de-asserted while yielding to the LoRa cap's driven pins. See setup() in
    // main.cpp for the full rationale.
    //
    // Cardputer-only: these pins are the expansion-header cap chip-selects,
    // defined in pins.h solely under TARGET_CARDPUTER. The StickC has no
    // expansion header and no built-in SD, and returns above before reaching
    // here — so this block is dead on that target as well as undefinable.
#if defined(TARGET_CARDPUTER)
    pinMode(pins::LORA_NSS, OUTPUT);
    digitalWrite(pins::LORA_NSS, HIGH);
    pinMode(pins::CC1101_CS, INPUT_PULLUP);
    pinMode(pins::NRF24_CS, INPUT_PULLUP);
#endif

    // Get pins from M5Unified
    int8_t sd_clk = M5.getPin(m5::sd_spi_sclk);
    int8_t sd_mosi = M5.getPin(m5::sd_spi_mosi);
    int8_t sd_miso = M5.getPin(m5::sd_spi_miso);
    int8_t sd_cs = M5.getPin(m5::sd_spi_cs);
    
    // Fallback to config pins if M5Unified didn't provide them
    if (sd_cs < 0 || sd_clk < 0 || sd_mosi < 0 || sd_miso < 0) {
        Serial.println("[SDManager] Using config pin defaults");
        sd_cs = pins::SD_CS;
        sd_clk = pins::SD_CLK;
        sd_mosi = pins::SD_MOSI;
        sd_miso = pins::SD_MISO;
    }
    
    Serial.printf("[SDManager] Pins - CS:%d CLK:%d MOSI:%d MISO:%d\n", sd_cs, sd_clk, sd_mosi, sd_miso);

    // Method 1: Try if SD is already mounted by M5Launcher/bootloader
    Serial.println("[SDManager] Checking if SD already mounted...");
    if (SD.cardType() != CARD_NONE) {
        File root = SD.open("/");
        if (root) {
            root.close();
            Serial.println("[SDManager] SD card already mounted by launcher!");
            m_status = SDStatus::READY;
            m_initialized = true;
            createDirectoryStructure();
            return true;
        }
    }
    
    // Method 2: Try Arduino SD with dedicated SPI bus (FSPI = SPI2 on ESP32-S3).
    // sdSPI is file-scoped so the multi-radio cap probe can share this exact
    // bus instance (slice-0002).
    Serial.println("[SDManager] Attempting Arduino SD init with FSPI...");

    // Initialize SPI with SD card pins. The bus is now owned and shareable with
    // the cap probe regardless of whether a card mounts below, decoupling cap
    // detection from card presence (a seated cap must be probeable with no card).
    sdSPI.begin(sd_clk, sd_miso, sd_mosi, sd_cs);
    g_sdBusBegun = true;
    
    // Try mounting at different frequencies (Safer speed first: 4MHz)
    const uint32_t frequencies[] = {4000000, 10000000, 20000000, 1000000, 400000};
    const char* freq_names[] = {"4MHz", "10MHz", "20MHz", "1MHz", "400kHz"};
    
    bool success = false;
    
    for (int i = 0; i < 5 && !success; i++) {
        Serial.printf("[SDManager] Trying %s... ", freq_names[i]);
        
        // End any previous attempt
        SD.end();
        delay(100);
        
        if (SD.begin(sd_cs, sdSPI, frequencies[i])) {
            if (SD.cardType() != CARD_NONE) {
                // Verification: Try to open the root directory to confirm access
                File root = SD.open("/");
                if (root) {
                    Serial.printf("SUCCESS (type=%d)\n", SD.cardType());
                    root.close();
                    success = true;
                } else {
                    Serial.println("mount ok but root unreadable (speed too high?)");
                }
            } else {
                Serial.println("mounted but no card");
            }
        } else {
            Serial.println("failed");
        }
    }
    
    // No Method 3 default-SPI fallback: SPI.begin() on these same pins re-inits a
    // second peripheral over the FSPI instance above (logs "addApbChangeCallback:
    // duplicate func") and the first SD.begin() on it spins forever in sdWait()
    // when no card is present — the measured no-card boot freeze (slice-0020). The
    // dedicated-FSPI ladder above is the proven and only working mount path on this
    // board, so a failure here means no usable card.
    if (!success) {
        Serial.println("[SDManager] No card mounted on the dedicated FSPI bus.");
        Serial.println("[SDManager] Note: If M5Launcher sees the card, try direct USB flash");
        m_status = SDStatus::NO_CARD;
        m_noCardVerdict = true;  // don't re-run the ladder on shared-bus re-syncs
        return false;
    }

    // Print card info
    Serial.printf("[SDManager] Card Type: %s\n", 
        SD.cardType() == CARD_SD ? "SD" : 
        SD.cardType() == CARD_SDHC ? "SDHC" : 
        SD.cardType() == CARD_MMC ? "MMC" : "Unknown");
    Serial.printf("[SDManager] Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));

    m_status = SDStatus::READY;
    m_initialized = true;
    m_noCardVerdict = false;

    // Create directory structure
    createDirectoryStructure();

    return true;
}

void SDManager::deinit() {
    if (m_initialized) {
        SD.end();
        m_initialized = false;
        m_status = SDStatus::NOT_INITIALIZED;
    }
}

SPIClass* SDManager::spiBus() {
    return g_sdBusBegun ? &sdSPI : nullptr;
}

bool SDManager::remount(bool forceRetry) {
    // Once a cardless boot is known (no card-detect pin, so the verdict cost the
    // full FSPI ladder once), don't pay that ladder again on every shared-bus
    // re-sync. Only an explicit operator retry (forceRetry) re-attempts.
    if (!sdShouldAttemptRemount(m_noCardVerdict, forceRetry)) {
        Serial.println("[SDManager] remount skipped: no card (cached verdict)");
        return false;
    }
    if (forceRetry) {
        m_noCardVerdict = false;
    }

    // Tear the driver down so init() re-runs the full mount (Method 1's
    // cardType() reads CARD_NONE after SD.end(), so it takes the dedicated-SPI
    // path and re-syncs the card from CMD0) rather than short-circuiting on
    // m_initialized.
    deinit();
    delay(50);
    return init();
}

SDCardInfo SDManager::getCardInfo() const {
    SDCardInfo info = {0, 0, 0, "UNKNOWN"};
    
    if (!m_initialized) {
        return info;
    }

    info.totalBytes = SD.totalBytes();
    info.usedBytes = SD.usedBytes();
    info.freeBytes = info.totalBytes - info.usedBytes;

    switch (SD.cardType()) {
        case CARD_MMC:  info.type = "MMC";  break;
        case CARD_SD:   info.type = "SD";   break;
        case CARD_SDHC: info.type = "SDHC"; break;
        default:        info.type = "UNKNOWN"; break;
    }

    return info;
}

bool SDManager::createDirectoryStructure() {
    if (!m_initialized) return false;

    // Create all required directories
    const char* dirs[] = {
        config::SD_BASE_PATH,
        config::SD_CONFIG_PATH,
        config::SD_CAPTURES_PATH,
        config::SD_HANDSHAKES_PATH,
        config::SD_PACKETS_PATH,
        config::SD_LOGS_PATH
    };

    for (const char* dir : dirs) {
        if (!SD.exists(dir)) {
            if (!SD.mkdir(dir)) {
                return false;
            }
        }
    }

    return true;
}

bool SDManager::fileExists(const char* path) const {
    if (!m_initialized) return false;
    return SD.exists(path);
}

bool SDManager::directoryExists(const char* path) const {
    if (!m_initialized) return false;
    
    File dir = SD.open(path);
    if (!dir) return false;
    
    bool isDir = dir.isDirectory();
    dir.close();
    return isDir;
}

bool SDManager::createDirectory(const char* path) {
    if (!m_initialized) return false;
    
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
}

bool SDManager::deleteFile(const char* path) {
    if (!m_initialized) return false;
    return SD.remove(path);
}

bool SDManager::deleteDirectory(const char* path) {
    if (!m_initialized) return false;
    return SD.rmdir(path);
}

bool SDManager::renameFile(const char* oldPath, const char* newPath) {
    if (!m_initialized) return false;
    return SD.rename(oldPath, newPath);
}

size_t SDManager::getFileSize(const char* path) const {
    if (!m_initialized) return 0;
    
    File file = SD.open(path, FILE_READ);
    if (!file) return 0;
    
    size_t size = file.size();
    file.close();
    return size;
}

FileResult SDManager::writeFile(const char* path, const uint8_t* data, size_t size) {
    FileResult result = {false, nullptr, 0, 0};
    
    if (!m_initialized) {
        result.error = "SD not initialized";
        return result;
    }

    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        result.error = "Failed to open file";
        return result;
    }

    result.bytesWritten = file.write(data, size);
    file.close();

    if (result.bytesWritten != size) {
        result.error = "Write incomplete";
        return result;
    }

    result.success = true;
    return result;
}

FileResult SDManager::writeFile(const char* path, const char* content) {
    return writeFile(path, reinterpret_cast<const uint8_t*>(content), strlen(content));
}

FileResult SDManager::appendFile(const char* path, const uint8_t* data, size_t size) {
    FileResult result = {false, nullptr, 0, 0};
    
    if (!m_initialized) {
        result.error = "SD not initialized";
        return result;
    }

    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        result.error = "Failed to open file";
        return result;
    }

    result.bytesWritten = file.write(data, size);
    file.close();

    if (result.bytesWritten != size) {
        result.error = "Write incomplete";
        return result;
    }

    result.success = true;
    return result;
}

FileResult SDManager::appendFile(const char* path, const char* content) {
    return appendFile(path, reinterpret_cast<const uint8_t*>(content), strlen(content));
}

FileResult SDManager::readFile(const char* path, uint8_t* buffer, size_t bufferSize) {
    FileResult result = {false, nullptr, 0, 0};
    
    if (!m_initialized) {
        result.error = "SD not initialized";
        return result;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        result.error = "Failed to open file";
        return result;
    }

    size_t fileSize = file.size();
    size_t toRead = (fileSize < bufferSize) ? fileSize : bufferSize;
    
    result.bytesRead = file.read(buffer, toRead);
    file.close();

    if (result.bytesRead != toRead) {
        result.error = "Read incomplete";
        return result;
    }

    result.success = true;
    return result;
}

void SDManager::generateTimestampFilename(const char* prefix, const char* extension,
                                           char* outBuffer, size_t bufferSize) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    snprintf(outBuffer, bufferSize, "%s_%04d%02d%02d_%02d%02d%02d%s",
             prefix,
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec,
             extension);
}

uint8_t SDManager::getFreeSpacePercent() const {
    if (!m_initialized) return 0;
    
    SDCardInfo info = getCardInfo();
    if (info.totalBytes == 0) return 0;
    
    return static_cast<uint8_t>((info.freeBytes * 100) / info.totalBytes);
}

bool SDManager::hasSpaceFor(size_t requiredBytes) const {
    if (!m_initialized) return false;
    
    SDCardInfo info = getCardInfo();
    return info.freeBytes >= requiredBytes;
}

#else // UNIT_TEST - Mock implementation

bool SDManager::init() {
    m_status = SDStatus::READY;
    m_initialized = true;
    m_noCardVerdict = false;
    return true;
}

void SDManager::deinit() {
    m_initialized = false;
    m_status = SDStatus::NOT_INITIALIZED;
}

SPIClass* SDManager::spiBus() { return nullptr; }

bool SDManager::remount(bool forceRetry) {
    if (!sdShouldAttemptRemount(m_noCardVerdict, forceRetry)) {
        return false;
    }
    if (forceRetry) {
        m_noCardVerdict = false;
    }
    return init();
}

SDCardInfo SDManager::getCardInfo() const {
    return {1024 * 1024 * 1024, 100 * 1024 * 1024, 924 * 1024 * 1024, "MOCK"};
}

bool SDManager::createDirectoryStructure() { return true; }
bool SDManager::fileExists(const char*) const { return false; }
bool SDManager::directoryExists(const char*) const { return false; }
bool SDManager::createDirectory(const char*) { return true; }
bool SDManager::deleteFile(const char*) { return true; }
bool SDManager::deleteDirectory(const char*) { return true; }
bool SDManager::renameFile(const char*, const char*) { return true; }
size_t SDManager::getFileSize(const char*) const { return 0; }

FileResult SDManager::writeFile(const char*, const uint8_t*, size_t size) {
    return {true, nullptr, size, 0};
}

FileResult SDManager::writeFile(const char* path, const char* content) {
    return writeFile(path, reinterpret_cast<const uint8_t*>(content), strlen(content));
}

FileResult SDManager::appendFile(const char*, const uint8_t*, size_t size) {
    return {true, nullptr, size, 0};
}

FileResult SDManager::appendFile(const char* path, const char* content) {
    return appendFile(path, reinterpret_cast<const uint8_t*>(content), strlen(content));
}

FileResult SDManager::readFile(const char*, uint8_t*, size_t) {
    return {true, nullptr, 0, 0};
}

void SDManager::generateTimestampFilename(const char* prefix, const char* extension,
                                           char* outBuffer, size_t bufferSize) {
    snprintf(outBuffer, bufferSize, "%s_20260103_120000%s", prefix, extension);
}

uint8_t SDManager::getFreeSpacePercent() const { return 90; }
bool SDManager::hasSpaceFor(size_t) const { return true; }

#endif // UNIT_TEST

} // namespace adversary
