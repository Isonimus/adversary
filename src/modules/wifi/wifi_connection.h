/**
 * @file wifi_connection.h
 * @brief WiFi Station (STA) connection manager
 * 
 * Manages device connection to WiFi networks for traffic sniffing.
 * Supports credential persistence for auto-connect at startup.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>

#ifdef ESP32
#include <Arduino.h>
#include <WiFi.h>
#endif

namespace adversary {

/**
 * @brief WiFi connection state
 */
enum class ConnectionState : uint8_t {
    DISCONNECTED,   ///< Not connected to any network
    CONNECTING,     ///< Connection in progress
    CONNECTED,      ///< Successfully connected
    FAILED,         ///< Connection failed (wrong password, etc)
    TIMEOUT         ///< Connection timed out
};

/**
 * @brief WiFi Station connection manager
 * 
 * Singleton class that manages WiFi STA mode connections.
 * Credentials can be saved to SD for auto-connect at startup.
 */
class WiFiConnection {
public:
    static WiFiConnection& getInstance() {
        static WiFiConnection instance;
        return instance;
    }
    
    // Prevent copying
    WiFiConnection(const WiFiConnection&) = delete;
    WiFiConnection& operator=(const WiFiConnection&) = delete;
    
    /**
     * @brief Connect to a WiFi network
     * @param ssid Network SSID
     * @param password Network password (empty for open networks)
     * @param channel Optional channel hint (0 = auto)
     * @param save Whether to save credentials for auto-connect
     * @return true if connection started successfully
     */
    bool connect(const char* ssid, const char* password, 
                 uint8_t channel = 0, bool save = true);
    
    /**
     * @brief Disconnect from current network
     */
    void disconnect();
    
    /**
     * @brief Check if connected to any network
     */
    bool isConnected() const;
    
    /**
     * @brief Check if connected to a specific network
     * @param ssid SSID to check
     */
    bool isConnectedTo(const char* ssid) const;
    
    /**
     * @brief Update connection state (call in main loop)
     * Handles connection timeout and state transitions
     */
    void update();
    
    // =========================================================================
    // Credential Persistence
    // =========================================================================
    
    /**
     * @brief Save current credentials to settings
     */
    void saveCredentials();
    
    /**
     * @brief Clear saved credentials from settings
     */
    void forgetCredentials();
    
    /**
     * @brief Check if credentials are saved
     */
    bool hasSavedCredentials() const;
    
    /**
     * @brief Attempt to connect using saved credentials
     * Called at startup if credentials exist
     */
    void autoConnect();
    
    // =========================================================================
    // Status Getters
    // =========================================================================
    
    /**
     * @brief Get current connection state
     */
    ConnectionState getState() const { return state_; }
    
    /**
     * @brief Get connected/connecting SSID
     */
    const char* getSSID() const { return ssid_; }
    
    /**
     * @brief Get saved SSID (may differ from current)
     */
    const char* getSavedSSID() const;
    
    /**
     * @brief Get saved password (from SettingsManager for auto-connect)
     */
    const char* getSavedPassword() const;
    
#ifdef ESP32
    /**
     * @brief Get local IP address (when connected)
     */
    IPAddress getLocalIP() const;
    
    /**
     * @brief Get current RSSI (when connected)
     */
    int8_t getRSSI() const;
#endif
    
    // =========================================================================
    // Callbacks
    // =========================================================================
    
    using ConnectionCallback = std::function<void(bool success, ConnectionState state)>;
    
    /**
     * @brief Set callback for connection result
     */
    void setOnConnectionResult(ConnectionCallback callback) {
        onConnectionResult_ = callback;
    }

private:
    WiFiConnection();
    ~WiFiConnection() = default;
    
    void setState(ConnectionState newState);
    
    ConnectionState state_ = ConnectionState::DISCONNECTED;
    char ssid_[33] = {0};
    char password_[65] = {0};
    uint8_t channel_ = 0;
    
    uint32_t connectStartTime_ = 0;
    bool saveOnConnect_ = false;
    
    ConnectionCallback onConnectionResult_;
    
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 30000;  // 30 seconds
};

// =============================================================================
// State name helper (for debug/display)
// =============================================================================

inline const char* getConnectionStateName(ConnectionState state) {
    switch (state) {
        case ConnectionState::DISCONNECTED: return "Disconnected";
        case ConnectionState::CONNECTING:   return "Connecting";
        case ConnectionState::CONNECTED:    return "Connected";
        case ConnectionState::FAILED:       return "Failed";
        case ConnectionState::TIMEOUT:      return "Timeout";
        default:                            return "Unknown";
    }
}

} // namespace adversary
