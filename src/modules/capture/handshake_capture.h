/**
 * @file handshake_capture.h
 * @brief WPA/WPA2 4-way handshake capture module
 * 
 * Captures and validates WPA/WPA2 4-way handshakes for offline cracking.
 * Integrates with packet sniffer for EAPOL frame detection and deauth
 * for forcing client reconnections.
 */

#ifndef ADVERSARY_HANDSHAKE_CAPTURE_H
#define ADVERSARY_HANDSHAKE_CAPTURE_H

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <cstring>
#include <vector>

#ifdef ESP32
#include <esp_wifi.h>
#endif

namespace adversary {

/**
 * @brief EAPOL key information flags
 */
struct EAPOLKeyInfo {
    bool pairwise;      ///< Pairwise key (vs group key)
    bool install;       ///< Install key flag
    bool keyAck;        ///< Key ACK flag (AP to client)
    bool keyMIC;        ///< Message has MIC
    bool secure;        ///< Secure bit set
    bool error;         ///< Error occurred
    bool request;       ///< Request flag
    bool encrypted;     ///< Key data encrypted
    uint8_t keyDescVer; ///< Key descriptor version (1=HMAC-MD5, 2=HMAC-SHA1, 3=AES-CMAC)
};

/**
 * @brief Handshake message type (1-4)
 */
enum class HandshakeMessage : uint8_t {
    UNKNOWN = 0,
    MSG1 = 1,   ///< AP -> Client: ANonce
    MSG2 = 2,   ///< Client -> AP: SNonce + MIC
    MSG3 = 3,   ///< AP -> Client: ANonce + MIC + Install
    MSG4 = 4    ///< Client -> AP: MIC (confirmation)
};

/**
 * @brief Handshake capture state
 */
enum class HandshakeState : uint8_t {
    IDLE,           ///< Not capturing
    WAITING,        ///< Waiting for handshake
    GOT_MSG1,       ///< Captured message 1
    GOT_MSG2,       ///< Captured messages 1-2
    GOT_MSG3,       ///< Captured messages 1-3
    CRACKABLE,      ///< Minimum valid handshake (M2 + ANonce) - sufficient for hashcat/aircrack-ng
    COMPLETE,       ///< Full 4-way handshake captured (all 4 messages)
    TIMEOUT,        ///< Capture timed out
    ERROR           ///< Error occurred
};

/**
 * @brief Statistics for handshake capture
 */
struct HandshakeStats {
    uint32_t eapolFrames;       ///< Total EAPOL frames seen
    uint32_t msg1Count;         ///< Message 1 frames
    uint32_t msg2Count;         ///< Message 2 frames
    uint32_t msg3Count;         ///< Message 3 frames
    uint32_t msg4Count;         ///< Message 4 frames
    uint32_t deauthsSent;       ///< Deauth frames sent
    uint32_t captureStartTime;  ///< Capture start timestamp
    uint32_t handshakeTime;     ///< Time when handshake completed
    
    void reset() {
        eapolFrames = 0;
        msg1Count = 0;
        msg2Count = 0;
        msg3Count = 0;
        msg4Count = 0;
        deauthsSent = 0;
        captureStartTime = 0;
        handshakeTime = 0;
    }
    
    uint32_t getDurationMs() const;
};

/**
 * @brief Captured PMKID data (clientless attack)
 * 
 * PMKID is extracted from M1 EAPOL frame's RSN IE.
 * Can be cracked without client handshake using hashcat mode 22000.
 */
struct CapturedPMKID {
    uint8_t bssid[6];           ///< Access point BSSID
    uint8_t staMac[6];          ///< Station MAC (our MAC when probing)
    uint8_t pmkid[16];          ///< The 16-byte PMKID
    char ssid[33];              ///< Network SSID
    uint32_t timestamp;         ///< Capture timestamp
    bool saved;                 ///< Already saved to file
    
    void reset() {
        memset(this, 0, sizeof(*this));
    }
};

/**
 * @brief Captured handshake data
 */
struct CapturedHandshake {
    uint8_t apBssid[6];         ///< Access point BSSID
    uint8_t clientMac[6];       ///< Client MAC address
    char ssid[33];              ///< Network SSID
    uint8_t channel;            ///< WiFi channel
    
    // Nonces from handshake
    uint8_t anonce[32];         ///< Authenticator nonce (from AP)
    uint8_t snonce[32];         ///< Supplicant nonce (from client)
    
    // MIC for validation
    uint8_t mic[16];            ///< Message Integrity Code
    
    // Key version
    uint8_t keyDescVer;         ///< 1=MD5, 2=SHA1, 3=AES-CMAC
    
    // Frame data for PCAP
    bool hasMsg1;
    bool hasMsg2;
    bool hasMsg3;
    bool hasMsg4;
    
    // Raw EAPOL frames (for PCAP export)
    uint8_t msg1Data[256];
    uint16_t msg1Len;
    uint8_t msg2Data[256];
    uint16_t msg2Len;
    uint8_t msg3Data[512];      ///< Larger for encrypted key data
    uint16_t msg3Len;
    uint8_t msg4Data[256];
    uint16_t msg4Len;
    
    // Beacon frame (required for hashcat PCAP). std::vector so the struct has
    // correct value semantics: it is stored BY VALUE in std::vector<CapturedHandshake>
    // (session buffers), and a raw owning pointer here was shallow-copied across
    // those copies -> use-after-free + leaks over long auto-hunt sessions.
    std::vector<uint8_t> beaconData;

    // Signal strength
    int8_t signalStrength = 0;      ///< RSSI in dBm

    bool hasBeacon() const { return !beaconData.empty(); }
    
    /**
     * @brief Full 4-way handshake captured
     */
    bool isValid() const {
        return hasMsg1 && hasMsg2 && hasMsg3 && hasMsg4;
    }
    
    /**
     * @brief Minimum for saving (M2 has SNonce + MIC, need ANonce from M1 or M3)
     */
    bool isMinimumValid() const {
        return hasMsg2 && (hasMsg1 || hasMsg3);
    }
    
    /**
     * @brief Check if handshake is valid for WPA-SEC upload
     * WPA-SEC needs: Beacon + M1 + M2 (minimum valid pair with all nonces)
     */
    bool isWpaSecValid() const {
        return hasBeacon() && hasMsg1 && hasMsg2;
    }
    
    /**
     * @brief Get frame status string for UI display (e.g., "M1M2" or "M1M2M3M4")
     */
    const char* getFrameStatus() const {
        static char status[16];
        status[0] = '\0';
        if (hasMsg1) strcat(status, "M1");
        if (hasMsg2) strcat(status, "M2");
        if (hasMsg3) strcat(status, "M3");
        if (hasMsg4) strcat(status, "M4");
        return status;
    }
    
    void reset() {
        // Reset every field to defaults. Assigning a fresh instance frees the
        // beacon vector and zeroes the PODs — must NOT memset(this, ...) anymore,
        // that would clobber the std::vector's internals and leak its buffer.
        *this = CapturedHandshake();
    }

    void setBeacon(const uint8_t* data, uint16_t len) {
        beaconData.clear();
        if (data && len > 0 && len <= 512) {
            beaconData.assign(data, data + len);
        }
    }
};

/**
 * @brief Configuration for handshake capture
 */
struct HandshakeCaptureConfig {
    uint8_t targetBssid[6];     ///< Target AP BSSID
    char targetSsid[33];        ///< Target SSID
    uint8_t channel;            ///< Channel to monitor
    bool autoDeauth;            ///< Auto-send deauth to force reconnect
    uint8_t deauthCount;        ///< Number of deauths to send
    uint16_t deauthInterval;    ///< Interval between deauth bursts (ms)
    uint32_t timeoutMs;         ///< Capture timeout (0 = no timeout)
    bool captureAllClients;     ///< Capture from any client (vs specific)
    uint8_t targetClient[6];    ///< Specific client to target (if not captureAllClients)
    
    HandshakeCaptureConfig() {
        memset(targetBssid, 0, 6);
        memset(targetSsid, 0, 33);
        channel = 1;
        autoDeauth = true;
        deauthCount = 5;
        deauthInterval = 500;
        timeoutMs = 60000;  // 60 second default timeout
        captureAllClients = true;
        memset(targetClient, 0, 6);
    }
    
    bool hasValidTarget() const {
        for (int i = 0; i < 6; i++) {
            if (targetBssid[i] != 0) return true;
        }
        return false;
    }
};

/**
 * @brief Handshake capture module
 * 
 * Monitors EAPOL frames to capture WPA/WPA2 4-way handshakes.
 * Can integrate with deauth attack to force client reconnections.
 */
class HandshakeCapture {
public:
    
    /**
     * @brief Get singleton instance
     */
    static HandshakeCapture& getInstance();
    
    // Prevent copying
    HandshakeCapture(const HandshakeCapture&) = delete;
    HandshakeCapture& operator=(const HandshakeCapture&) = delete;
    
    /**
     * @brief Initialize the module
     * @return true on success
     */
    bool init();
    
    /**
     * @brief Deinitialize and cleanup
     */
    void deinit();
    
    /**
     * @brief Start handshake capture
     * @param config Capture configuration
     * @return true if started successfully
     */
    bool start(const HandshakeCaptureConfig& config);
    
    /**
     * @brief Stop capture
     */
    void stop();
    
    /**
     * @brief Update (call in main loop)
     */
    void update();
    
    /**
     * @brief Process an EAPOL packet
     * @param data Raw packet data (full 802.11 frame)
     * @param length Packet length
     * @param rssi Signal strength
     */
    void processEAPOL(const uint8_t* data, uint16_t length, int8_t rssi);
    
    // Getters
    HandshakeState getState() const { return state_; }
    const HandshakeStats& getStats() const { return stats_; }
    const CapturedHandshake& getHandshake() const { return handshake_; }
    bool isCapturing() const { return state_ == HandshakeState::WAITING || 
                                      state_ == HandshakeState::GOT_MSG1 ||
                                      state_ == HandshakeState::GOT_MSG2 ||
                                      state_ == HandshakeState::GOT_MSG3; }
    bool isComplete() const { return state_ == HandshakeState::COMPLETE || 
                                     state_ == HandshakeState::CRACKABLE; }
    
    /**
     * @brief Check if PMKID was captured
     */
    bool hasPMKID() const { return hasPMKID_; }
    
    /**
     * @brief Get captured PMKID
     */
    const CapturedPMKID& getPMKID() const { return pmkid_; }
    
    /**
     * @brief Parse EAPOL key info from packet
     */
    static EAPOLKeyInfo parseKeyInfo(uint16_t keyInfo);
    
    /**
     * @brief Determine handshake message type from EAPOL frame
     */
    static HandshakeMessage identifyMessage(const uint8_t* eapolData, uint16_t length);
    
    /**
     * @brief Get message type as string
     */
    static const char* getMessageString(HandshakeMessage msg);
    
    /**
     * @brief Get state as string
     */
    static const char* getStateString(HandshakeState state);

private:
    HandshakeCapture();
    ~HandshakeCapture() = default;
    
    void setState(HandshakeState newState);
    void processMessage1(const uint8_t* eapolData, uint16_t eapolLen,
                         const uint8_t* frameData, uint16_t frameLen);
    void processMessage2(const uint8_t* eapolData, uint16_t eapolLen,
                         const uint8_t* frameData, uint16_t frameLen);
    void processMessage3(const uint8_t* eapolData, uint16_t eapolLen,
                         const uint8_t* frameData, uint16_t frameLen);
    void processMessage4(const uint8_t* eapolData, uint16_t eapolLen,
                         const uint8_t* frameData, uint16_t frameLen);
    void updateCaptureState();  ///< Check what we have and update state
    void sendDeauth();  // Sends both deauth + disassoc frames
    void checkTimeout();
    bool extractClientMac(const uint8_t* frame, uint8_t* clientMac);
    void extractPMKID(const uint8_t* keyData, uint16_t keyDataLen);
    
    // Static promiscuous mode callback
#ifdef ESP32
    static void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);
#endif
    static HandshakeCapture* s_instance_;
    
    HandshakeState state_;
    HandshakeCaptureConfig config_;
    HandshakeStats stats_;
    CapturedHandshake handshake_{};  // value-init: zeroed PODs + valid empty beacon vector
    CapturedPMKID pmkid_;
    bool hasPMKID_ = false;
    
    bool initialized_;
    uint32_t lastDeauthTime_;
    uint32_t deauthBurstCount_;
    
    // Grace period for waiting for remaining handshake messages
    uint32_t graceStartTime_;          ///< When minimum valid handshake was reached
    bool waitingForComplete_;          ///< True if waiting for M3/M4 after M1+M2
    static constexpr uint32_t GRACE_PERIOD_MS = 500;  ///< Wait 500ms for remaining messages
    
    // Pre-deauth delay: let promiscuous mode stabilize and catch M1 from already-connecting clients
    static constexpr uint32_t PRE_DEAUTH_DELAY_MS = 500;  ///< Wait 500ms before first deauth
    
    // Discovered clients for targeted deauth
    static constexpr uint8_t MAX_DISCOVERED_CLIENTS = 8;
    uint8_t discoveredClients_[MAX_DISCOVERED_CLIENTS][6];
    uint8_t discoveredClientCount_ = 0;
    void addDiscoveredClient(const uint8_t* clientMac);
    
    static constexpr uint16_t EAPOL_KEY_INFO_OFFSET = 5;   ///< Offset to key info in EAPOL-Key
    static constexpr uint16_t EAPOL_NONCE_OFFSET = 17;     ///< Offset to nonce
    static constexpr uint16_t EAPOL_MIC_OFFSET = 81;       ///< Offset to MIC
    static constexpr uint8_t EAPOL_NONCE_LEN = 32;
    static constexpr uint8_t EAPOL_MIC_LEN = 16;
};

} // namespace adversary

#endif // ADVERSARY_HANDSHAKE_CAPTURE_H
