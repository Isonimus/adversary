/**
 * @file pwncrack_service.h
 * @brief pwncrack.org cloud cracking service integration
 *
 * Parallel to WpaSecService, but speaks pwncrack's API: it uploads hashcat
 * .hc22000 lines (the .22000 files we generate for PMKID *and* 4-way captures)
 * and downloads the cracked potfile. pwncrack does NOT accept raw .pcap — the
 * .22000 is what makes it useful, complementing WPA-SEC (which takes .pcap).
 *
 *   POST /upload_handshake         multipart: key=<apiKey>, handshake=<file.hc22000>
 *   GET  /download_potfile_script?key=<apiKey>   colon-delimited potfile
 *
 * Status is tracked in the manifest's parallel pwncrack* fields via the
 * registry; the WpaSecStatus enum is reused (same NOT_UPLOADED/UPLOADED/CRACKED
 * semantics).
 */

#pragma once

#include <cstdint>
#include "wpasec_service.h"  // for WpaSecStatus enum

namespace adversary {

class PwncrackService {
public:
    static PwncrackService& getInstance() {
        static PwncrackService instance;
        return instance;
    }

    PwncrackService(const PwncrackService&) = delete;
    PwncrackService& operator=(const PwncrackService&) = delete;

    /**
     * @brief Upload a .22000 (hashcat hc22000) file to pwncrack.
     * @param filepath full path to the .22000 file
     * @param quiet when true (bulk sync), suppress the per-file success/skip
     *        toasts so the screen's own progress + live list show through.
     * @return true if the server accepted it (status marked UPLOADED)
     */
    bool uploadHandshake(const char* filepath, bool quiet = false);

    /**
     * @brief Download the pwncrack potfile and apply cracked passwords.
     * @param quiet when true (combined refresh), suppress the error / "no results"
     *        toasts so a 404 (no potfile for this key yet) doesn't shout over the
     *        WPA-SEC refresh result. A real crack still toasts.
     * @return number of results that matched a capture on THIS device
     */
    int fetchCrackedResults(bool quiet = false);

    /**
     * @brief pwncrack status for a capture (looked up by SSID in the registry).
     */
    WpaSecStatus getStatus(const char* ssid) const;

    /**
     * @brief True when a pwncrack API key is configured.
     */
    bool hasApiKey() const;

private:
    PwncrackService() = default;
    ~PwncrackService() = default;

    static constexpr const char* PWNCRACK_HOST = "pwncrack.org";
    static constexpr int PWNCRACK_PORT = 443;
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline bool PwncrackService::hasApiKey() const {
#ifdef ESP32
    extern bool _pwncrackHasApiKey();
    return _pwncrackHasApiKey();
#else
    return false;
#endif
}

} // namespace adversary
