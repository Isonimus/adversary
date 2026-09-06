/**
 * @file server_manager.h
 * @brief Manages AP, mDNS and WebServer for the Dashboard
 * 
 * Part of Phase 1B: Core Server implementation.
 */

#pragma once

#include <string>
#include <memory>

#ifdef ESP32
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#endif

namespace adversary {

/**
 * @brief Server Manager Singleton
 * 
 * Handles the lifecycle of the AP and the WebServer for the dashboard.
 */
class ServerManager {
public:
    static ServerManager& getInstance() {
        static ServerManager instance;
        return instance;
    }

    /**
     * @brief Start the server (AP + WebServer)
     * @return true if both started successfully
     */
    bool start();

    /**
     * @brief Stop the server and dismantle AP
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    bool isRunning() const { return running_; }

    /**
     * @brief Process web server requests
     */
    void update();

    /**
     * @brief Get count of connected stations
     */
    int getConnectedStations() const;

    /**
     * @brief Get server IP address (usually 192.168.4.1)
     */
    const char* getIPAddress() const;

    /**
     * @brief Get server uptime in seconds
     */
    uint32_t getUptime() const;

    /**
     * @brief Get total number of requests handled
     */
    uint32_t getRequestCount() const { return requestCount_; }

    /**
     * @brief Get time of the last request (millis)
     */
    uint32_t getLastRequestTime() const { return lastRequestTime_; }

private:
    ServerManager();
    ~ServerManager() = default;

    bool setupAP();
    bool setupMDNS();
    bool setupWebServer();

    // Route handlers
    void handleRoot();
    void handleNotFound();
    
    // API handlers
    void handleApiInfo();
    void handleApiSettings();
    void handleApiFilesCaptures();
    void handleApiFilesHandshakes();
    void handleApiFilesPackets();
    void handleApiFilesLogs();
    void handleApiFilesWardriving();
    void handleApiHandshakeDetail();
    void handleApiWardrivingDetail();
    
    // API Helpers
    void serveFiles(const char* path, const char* category, bool minimal = false, const char* extension = nullptr);
    String urlDecode(String str);
    bool checkAuth();

    bool running_;
    uint32_t startTime_;
    uint32_t requestCount_;
    uint32_t lastRequestTime_;

#ifdef ESP32
    std::unique_ptr<WebServer> server_;
    std::unique_ptr<DNSServer> dnsServer_;
#endif

    // Static instance storage for IP/State to avoid allocations in update
    char ipAddress_[16];
};

} // namespace adversary
