#pragma once

/**
 * @file handshake_save.h
 * @brief Unified handshake saving with toast feedback and error logging
 * 
 * Consolidates handshake save logic to ensure consistent behavior:
 * - PCAP file creation with radiotap headers
 * - Metadata JSON generation
 * - Capture registry updates
 * - User feedback via toasts
 * - Error logging to SD card
 */

#include <cstdint>
#include <cstring>
#include <string>

#include "../modules/capture/handshake_capture.h"
#include "../modules/storage/handshake_metadata.h"
#include "../modules/storage/capture_registry.h"
#include "../modules/pcap/pcap_writer.h"
#include "../modules/gps/gps_manager.h"
#include "../modules/system/time_manager.h"
#include "../config/config.h"
#include "handshake_utils.h"
#include "error_logger.h"
#include "filename_utils.h"

#ifdef ESP32
#include "../ui/components/toast_manager.h"
#endif

namespace adversary {
namespace handshake_save {

// ============================================================================
// hashcat 22000 (.hc22000) line generation
//
// pwncrack accepts .22000 lines (PMKID WPA*02 and 4-way WPA*01) — not raw
// .pcap — so we synthesise them on-device at capture time from the data we
// already keep in CapturedHandshake. WPA-SEC takes the .pcap; pwncrack takes
// the .22000; the two services are complementary.
// ============================================================================

/// Find the 802.1X/EAPOL payload inside a raw 802.11 frame by scanning for the
/// LLC/SNAP + EAPOL ethertype marker (AA AA 03 00 00 00 88 8E). Robust to QoS
/// headers / varying 802.11 header lengths. Returns the offset just past the
/// marker (start of the EAPOL frame) or -1 if not found.
inline int findEapolOffset(const uint8_t* frame, size_t len) {
    static const uint8_t marker[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
    if (!frame || len < 8) return -1;
    for (size_t i = 0; i + 8 <= len; ++i) {
        if (memcmp(frame + i, marker, 8) == 0) return static_cast<int>(i + 8);
    }
    return -1;
}

/// Build a hashcat WPA*01 (EAPOL) line from a captured 4-way handshake.
/// Format: WPA*01*MIC*MAC_AP*MAC_STA*ESSID*ANONCE*EAPOL*MESSAGEPAIR
/// Uses M2's EAPOL frame (carries SNonce + MIC) with the MIC field zeroed, and
/// the ANonce from M1/M3. @return true if a complete line was written to @p out.
inline bool buildEapolHc22000Line(const CapturedHandshake& hs, char* out, size_t outLen) {
    if (!out || outLen == 0) return false;
    // Need M2 (MIC + SNonce) plus an ANonce source (M1 or M3).
    if (!(hs.hasMsg2 && (hs.hasMsg1 || hs.hasMsg3))) return false;

    int off = findEapolOffset(hs.msg2Data, hs.msg2Len);
    if (off < 0) return false;
    const uint8_t* eapol = hs.msg2Data + off;
    int avail = static_cast<int>(hs.msg2Len) - off;
    // EAPOL-Key MIC sits at offset 81 (4B EAPOL hdr + 77B up to the MIC); need
    // at least the MIC + the 2B key-data-length that follows it.
    if (avail < 99) return false;

    // Trust the EAPOL length field (bytes 2..3, big-endian) to trim any trailing
    // 802.11 FCS/padding; fall back to what's available if it looks wrong.
    int declared = 4 + ((eapol[2] << 8) | eapol[3]);
    int eapolLen = (declared >= 99 && declared <= avail) ? declared : avail;

    static uint8_t buf[256];
    if (eapolLen > static_cast<int>(sizeof(buf))) eapolLen = sizeof(buf);
    memcpy(buf, eapol, eapolLen);
    for (int i = 81; i < 81 + 16 && i < eapolLen; ++i) buf[i] = 0;  // zero the MIC

    size_t pos = 0;
    auto appendStr = [&](const char* s) {
        while (*s && pos + 1 < outLen) out[pos++] = *s++;
    };
    auto appendHex = [&](const uint8_t* d, size_t n) {
        static const char H[] = "0123456789abcdef";
        for (size_t i = 0; i < n && pos + 2 < outLen; ++i) {
            out[pos++] = H[d[i] >> 4];
            out[pos++] = H[d[i] & 0x0F];
        }
    };

    appendStr("WPA*01*");
    appendHex(hs.mic, 16);                                              appendStr("*");
    appendHex(hs.apBssid, 6);                                          appendStr("*");
    appendHex(hs.clientMac, 6);                                        appendStr("*");
    appendHex(reinterpret_cast<const uint8_t*>(hs.ssid), strlen(hs.ssid)); appendStr("*");
    appendHex(hs.anonce, 32);                                          appendStr("*");
    appendHex(buf, eapolLen);
    // messagepair 00: ANonce from M1, MIC/SNonce from M2 (the authenticated pair).
    appendStr("*00");

    if (pos + 1 >= outLen) return false;  // truncated — don't emit a partial line
    out[pos] = '\0';
    return true;
}

/// Write the canonical "{ssid}.22000" file (same stem as the .pcap) containing
/// every hashcat line we can build for this capture: the WPA*01 4-way line and,
/// when present, the WPA*02 PMKID line. Overwrites so a re-capture regenerates.
inline bool writeHandshake22000(const char* ssid, const CapturedHandshake& hs,
                                bool hasPMKID, const CapturedPMKID& pmkid) {
#ifdef ESP32
    char eapolLine[1024];
    bool haveEapol = buildEapolHc22000Line(hs, eapolLine, sizeof(eapolLine));
    if (!haveEapol && !hasPMKID) return false;  // nothing pwncrack can use

    if (!SD.exists(config::SD_HANDSHAKES_PATH)) {
        SD.mkdir(config::SD_HANDSHAKES_PATH);
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/%s.22000", config::SD_HANDSHAKES_PATH, ssid);
    SD.remove(path);  // clean rewrite

    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[hc22000] Failed to open %s\n", path);
        return false;
    }
    if (haveEapol) {
        file.print(eapolLine);
        file.print("\n");
    }
    if (hasPMKID) {
        // WPA*02*PMKID*MAC_AP*MAC_STA*ESSID***
        file.print("WPA*02*");
        for (int i = 0; i < 16; i++) file.printf("%02x", pmkid.pmkid[i]);
        file.print("*");
        for (int i = 0; i < 6; i++) file.printf("%02x", pmkid.bssid[i]);
        file.print("*");
        for (int i = 0; i < 6; i++) file.printf("%02x", pmkid.staMac[i]);
        file.print("*");
        for (size_t i = 0; i < strlen(pmkid.ssid); i++) file.printf("%02x", (uint8_t)pmkid.ssid[i]);
        file.print("***\n");
    }
    file.close();
    Serial.printf("[hc22000] Wrote %s (eapol=%d pmkid=%d)\n", path, haveEapol, hasPMKID);
    return true;
#else
    (void)ssid; (void)hs; (void)hasPMKID; (void)pmkid;
    return false;
#endif
}

/**
 * @brief Result of a handshake save operation
 */
struct SaveResult {
    bool success;
    char filename[128];
    char message[64];
    
    SaveResult() : success(false) {
        filename[0] = '\0';
        message[0] = '\0';
    }
};

/**
 * @brief Save a captured handshake to SD card with full metadata
 * 
 * This is the unified save function that should be used everywhere.
 * It handles:
 * - Validation
 * - PCAP creation with radiotap headers
 * - Metadata JSON generation
 * - Capture registry update
 * - Toast feedback to user
 * - Error logging
 * 
 * @param capture The handshake capture module
 * @param pcapWriter PcapWriter instance to use for writing
 * @param showToast Whether to show toast notifications (default: true)
 * @return SaveResult with success status and filename
 */
inline SaveResult saveHandshakeToPcap(HandshakeCapture& capture, 
                                       PcapWriter& pcapWriter,
                                       bool showToast = true) {
    SaveResult result;
    const auto& hs = capture.getHandshake();
    
    // Validate handshake
    if (!hs.isMinimumValid()) {
        logger::logWarning("HandshakeSave", "Handshake incomplete - not saving");
        strncpy(result.message, "Handshake incomplete", sizeof(result.message) - 1);
        return result;
    }
    
    // Generate filename using simplified SSID-based naming
    char filename[80];
    filename_utils::getHandshakePath(hs.ssid, filename, sizeof(filename));
    
    // Check for existing capture and preserve cracked password if present
    WpaSecStatus preservedStatus = WpaSecStatus::NOT_UPLOADED;
    char preservedPassword[65] = {0};
    uint32_t preservedCrackedAt = 0;
    
    if (filename_utils::handshakeExists(hs.ssid)) {
        // Load existing metadata to check for cracked password
        const HandshakeMetadata* existingMeta = CaptureRegistry::getInstance().getMetadata(
            strrchr(filename, '/') ? strrchr(filename, '/') + 1 : filename);
        if (existingMeta && existingMeta->wpaSecStatus == WpaSecStatus::CRACKED) {
            preservedStatus = WpaSecStatus::CRACKED;
            strncpy(preservedPassword, existingMeta->wpaSecPassword, sizeof(preservedPassword) - 1);
            preservedCrackedAt = existingMeta->wpaSecCrackedAt;
#ifdef ESP32
            Serial.printf("[HandshakeSave] Preserving cracked password for %s\n", hs.ssid);
#endif
        }
    }
    
    // Open PCAP file
    if (!pcapWriter.open(filename, PcapLinkType::IEEE802_11_RADIOTAP)) {
        logger::logError("HandshakeSave", "Failed to open PCAP file");
#ifdef ESP32
        if (showToast) {
            ToastManager::getInstance().show("SD Write Failed!", ToastType::ERROR, ToastPriority::PRIORITY_HIGH, 3000);
        }
#endif
        strncpy(result.message, "SD Write Failed", sizeof(result.message) - 1);
        return result;
    }
    
    // Set channel for radiotap header
    pcapWriter.setChannel(hs.channel);
    
    // Write frames in order: Beacon -> M1 -> M2 -> M3 -> M4
    uint32_t timestamp = 0;
    int packetsWritten = 0;
    
#ifdef ESP32
    Serial.printf("[HandshakeSave] hasBeacon=%d beaconLen=%u\n", hs.hasBeacon(), (unsigned)hs.beaconData.size());
    Serial.printf("[HandshakeSave] hasM1=%d M1Len=%u, hasM2=%d M2Len=%u\n", hs.hasMsg1, hs.msg1Len, hs.hasMsg2, hs.msg2Len);
    Serial.printf("[HandshakeSave] hasM3=%d M3Len=%u, hasM4=%d M4Len=%u\n", hs.hasMsg3, hs.msg3Len, hs.hasMsg4, hs.msg4Len);
#endif
    
    if (hs.hasBeacon()) {
        const size_t beaconLen = hs.beaconData.size();
        // Sanity check beacon size (should be < 512, > 10 at minimum)
        if (beaconLen > 512 || beaconLen < 10) {
            Serial.printf("[HandshakeSave] WARNING: Suspicious beacon len: %u\n", (unsigned)beaconLen);
        }
        pcapWriter.writePacketWithRadiotap(hs.beaconData.data(), (uint16_t)beaconLen, timestamp++);
        packetsWritten++;
    }
    if (hs.hasMsg1 && hs.msg1Len > 0) {
        pcapWriter.writePacketWithRadiotap(hs.msg1Data, hs.msg1Len, timestamp++);
        packetsWritten++;
    }
    if (hs.hasMsg2 && hs.msg2Len > 0) {
        pcapWriter.writePacketWithRadiotap(hs.msg2Data, hs.msg2Len, timestamp++);
        packetsWritten++;
    }
    if (hs.hasMsg3 && hs.msg3Len > 0) {
        pcapWriter.writePacketWithRadiotap(hs.msg3Data, hs.msg3Len, timestamp++);
        packetsWritten++;
    }
    if (hs.hasMsg4 && hs.msg4Len > 0) {
        pcapWriter.writePacketWithRadiotap(hs.msg4Data, hs.msg4Len, timestamp++);
        packetsWritten++;
    }
    
    pcapWriter.close();
    
#ifdef ESP32
    Serial.printf("[HandshakeSave] Wrote %d packets, %u bytes\n", packetsWritten, pcapWriter.getBytesWritten());
#endif
    
    // Prepare metadata
    HandshakeMetadata metadata;
    strncpy(metadata.ssid, hs.ssid, sizeof(metadata.ssid) - 1);
    memcpy(metadata.bssid, hs.apBssid, 6);
    metadata.channel = hs.channel;
    strncpy(metadata.type, utils::getHandshakeTypeString(hs, capture.hasPMKID()), sizeof(metadata.type) - 1);
    
#ifdef ESP32
    // Real Unix epoch once NTP/GPS/SD time is available — this value now persists
    // into the manifest record, so avoid baking in seconds-since-boot.
    metadata.capturedAt = TimeManager::getInstance().isSynced()
                              ? (uint32_t)TimeManager::getInstance().now()
                              : (uint32_t)(millis() / 1000);
#else
    metadata.capturedAt = 0;
#endif
    
    metadata.hasMsg1 = hs.hasMsg1;
    metadata.hasMsg2 = hs.hasMsg2;
    metadata.hasMsg3 = hs.hasMsg3;
    metadata.hasMsg4 = hs.hasMsg4;
    metadata.hasPMKID = capture.hasPMKID();
    
    // Quality: 25% per EAPOL message
    int msgCount = (hs.hasMsg1 ? 1 : 0) + (hs.hasMsg2 ? 1 : 0) + 
                   (hs.hasMsg3 ? 1 : 0) + (hs.hasMsg4 ? 1 : 0);
    metadata.quality = msgCount * 25;
    metadata.signalStrength = hs.signalStrength;  // Use captured signal strength
    metadata.wpaSecStatus = hs.isWpaSecValid() ? WpaSecStatus::NOT_UPLOADED : WpaSecStatus::INCOMPLETE;
    
    // GPS geolocation (if module detected and has valid fix)
#ifdef ESP32
    Serial.printf("[HandshakeSave] g_gpsDetected=%d\n", adversary::ui::g_gpsDetected);
    if (adversary::ui::g_gpsDetected) {
        const auto& gpsData = GPSManager::getInstance().getCurrentData();
        Serial.printf("[HandshakeSave] GPS lastUpdate=%lu, valid=%d, sats=%d, fix=%d\n", 
                      gpsData.lastUpdateMs, gpsData.coordinate.valid, 
                      gpsData.coordinate.satellites, gpsData.coordinate.fixQuality);
        Serial.printf("[HandshakeSave] GPS lat=%.6f, lon=%.6f, alt=%.1f\n",
                      gpsData.coordinate.latitude, gpsData.coordinate.longitude, 
                      gpsData.coordinate.altitude);
        
        if (gpsData.coordinate.valid) {
            metadata.hasGPS = true;
            metadata.latitude = gpsData.coordinate.latitude;
            metadata.longitude = gpsData.coordinate.longitude;
            metadata.altitude = gpsData.coordinate.altitude;
            metadata.satellites = gpsData.coordinate.satellites;
            Serial.println("[HandshakeSave] GPS data saved to metadata!");
        } else {
            Serial.println("[HandshakeSave] GPS fix not valid - not saving");
        }
    } else {
        Serial.println("[HandshakeSave] GPS module not detected");
    }
#endif
    
    // Apply preserved password if we had a cracked password before
    if (preservedStatus == WpaSecStatus::CRACKED && preservedPassword[0] != '\0') {
        metadata.wpaSecStatus = WpaSecStatus::CRACKED;
        strncpy(metadata.wpaSecPassword, preservedPassword, sizeof(metadata.wpaSecPassword) - 1);
        metadata.wpaSecCrackedAt = preservedCrackedAt;
    }
    
    // Emit the canonical "{ssid}.22000" (hashcat lines) next to the .pcap so the
    // capture can be uploaded to pwncrack (which takes .22000, not .pcap). Includes
    // the WPA*01 4-way line and, if present, the WPA*02 PMKID line. Done BEFORE the
    // manifest write so the record records whether a .22000 exists (drives the
    // pwncrack sync count without per-file SD checks).
    metadata.has22000 = writeHandshake22000(hs.ssid, hs, capture.hasPMKID(), capture.getPMKID());

    // Persist via the registry only: addHandshake() writes a full BSSID-bearing
    // record to the manifest (the single source of truth). The per-file .json
    // sidecar was retired in the storage rework — no more dual-write/staleness.
    CaptureRegistry::getInstance().addHandshake(hs.ssid, &metadata, 0, metadata.wpaSecStatus);
    
    // Copy results
    strncpy(result.filename, filename, sizeof(result.filename) - 1);
    snprintf(result.message, sizeof(result.message), "Saved: %s", hs.ssid);
    result.success = true;
    
    // Show success toast
#ifdef ESP32
    if (showToast) {
        char toastMsg[48];
        snprintf(toastMsg, sizeof(toastMsg), "Saved: %.20s.pcap", hs.ssid);
        ToastManager::getInstance().show(toastMsg, ToastType::SUCCESS, ToastPriority::PRIORITY_MEDIUM, 2500);
    }
    
    Serial.printf("[HandshakeSave] Saved: %s (%d packets)\n", filename, packetsWritten);
#endif
    
    return result;
}

/**
 * @brief Save PMKID in hashcat 22000 format
 * 
 * @param pmkid CapturedPMKID data structure from HandshakeCapture
 * @param showToast Whether to show toast notification
 * @return SaveResult with success status and filename
 */
inline SaveResult savePMKIDToFile(const CapturedPMKID& pmkid, bool showToast = true) {
    SaveResult result;
    
    // Generate filename
    char filename[128];
    snprintf(filename, sizeof(filename), "%s/%s_%02X%02X%02X.22000", 
             config::SD_HANDSHAKES_PATH, pmkid.ssid,
             pmkid.bssid[3], pmkid.bssid[4], pmkid.bssid[5]);
    
#ifdef ESP32
    // Create directory if needed
    if (!SD.exists(config::SD_HANDSHAKES_PATH)) {
        SD.mkdir(config::SD_HANDSHAKES_PATH);
    }
    
    // Open file
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        logger::logError("PMKIDSave", "Failed to open file for PMKID");
        if (showToast) {
            ToastManager::getInstance().show("SD Write Failed!", ToastType::ERROR, ToastPriority::PRIORITY_HIGH, 3000);
        }
        strncpy(result.message, "SD Write Failed", sizeof(result.message) - 1);
        return result;
    }
    
    // Write hashcat 22000 format (mode 02 = PMKID only)
    // Format: WPA*02*PMKID*MAC_AP*MAC_STA*ESSID_HEX***
    file.print("WPA*02*");
    
    // PMKID (16 bytes hex)
    for (int i = 0; i < 16; i++) {
        file.printf("%02x", pmkid.pmkid[i]);
    }
    file.print("*");
    
    // MAC AP (BSSID)
    for (int i = 0; i < 6; i++) {
        file.printf("%02x", pmkid.bssid[i]);
    }
    file.print("*");
    
    // MAC STA
    for (int i = 0; i < 6; i++) {
        file.printf("%02x", pmkid.staMac[i]);
    }
    file.print("*");
    
    // ESSID hex
    for (size_t i = 0; i < strlen(pmkid.ssid); i++) {
        file.printf("%02x", (uint8_t)pmkid.ssid[i]);
    }
    file.print("***\n");
    
    file.close();
    
    // Update registry (track PMKID as handshake for H indicator)
    CaptureRegistry::getInstance().addHandshake(pmkid.ssid);
    
    // Success
    strncpy(result.filename, filename, sizeof(result.filename) - 1);
    snprintf(result.message, sizeof(result.message), "PMKID: %s", pmkid.ssid);
    result.success = true;
    
    if (showToast) {
        char toastMsg[48];
        snprintf(toastMsg, sizeof(toastMsg), "PMKID: %.20s", pmkid.ssid);
        ToastManager::getInstance().show(toastMsg, ToastType::SUCCESS, ToastPriority::PRIORITY_MEDIUM, 2500);
    }
    
    Serial.printf("[PMKIDSave] Saved: %s\n", filename);
#else
    // Native build - just mark success
    strncpy(result.filename, filename, sizeof(result.filename) - 1);
    result.success = true;
#endif
    
    return result;
}

} // namespace handshake_save
} // namespace adversary
