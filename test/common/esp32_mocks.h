/**
 * @file esp32_mocks.h
 * @brief ESP32 WiFi and networking mocks for native testing
 */

#pragma once

#ifndef ESP32

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace test_mocks {

// ESP32 error types
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

// WiFi types
typedef int wifi_promiscuous_pkt_type_t;
typedef int wifi_mode_t;

#define WIFI_PKT_MGMT 0
#define WIFI_PKT_DATA 1
#define WIFI_MODE_NULL 0
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
#define WIFI_IF_AP 1
#define WIFI_IF_STA 0
#define WIFI_SECOND_CHAN_NONE 0

// WiFi structures
struct wifi_pkt_rx_ctrl_t {
    int sig_len;
    int rssi;
    int channel;
};

struct wifi_promiscuous_pkt_t {
    wifi_pkt_rx_ctrl_t rx_ctrl;
    uint8_t payload[512];
};

struct wifi_promiscuous_filter_t {
    uint32_t filter_mask;
};

#define WIFI_PROMIS_FILTER_MASK_MGMT 1
#define WIFI_PROMIS_FILTER_MASK_DATA 2

// WiFi station list
struct wifi_sta_info_t {
    uint8_t mac[6];
    int rssi;
};

struct wifi_sta_list_t {
    wifi_sta_info_t sta[10];
    int num;
};

// Mock WiFi class
class MockWiFi {
public:
    std::string ssid_val;
    std::string psk_val;
    bool connected = false;
    
    std::string SSID() { return ssid_val; }
    std::string psk() { return psk_val; }
    bool isConnected() { return connected; }
    bool mode(int m) { return true; }
    bool begin(const char* ssid, const char* pass) { return true; }
    bool softAP(const char* ssid, const char* pass = nullptr, int ch = 1, int hidden = 0, int max = 4) { return true; }
    bool softAPdisconnect(bool wifioff) { return true; }
    uint8_t softAPgetStationNum() { return 0; }
    void softAPmacAddress(uint8_t* mac) { 
        static uint8_t defaultMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        memcpy(mac, defaultMac, 6);
    }
    
    class IPAddress {
    public:
        std::string toString() { return "192.168.4.1"; }
    };
    
    IPAddress softAPIP() { return IPAddress(); }
    IPAddress localIP() { return IPAddress(); }
};

extern MockWiFi WiFi;

// ESP WiFi function mocks
esp_err_t esp_wifi_set_promiscuous(bool en);
esp_err_t esp_wifi_set_promiscuous_rx_cb(void(*cb)(void*, wifi_promiscuous_pkt_type_t));
esp_err_t esp_wifi_set_promiscuous_filter(wifi_promiscuous_filter_t* filter);
esp_err_t esp_wifi_get_mode(wifi_mode_t* mode);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_80211_tx(int ifx, const void *buffer, int len, bool en_sys_seq);
esp_err_t esp_wifi_set_channel(uint8_t primary, uint8_t second);
esp_err_t esp_wifi_set_inactive_time(int ifx, uint16_t sec);
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t* list);

// ESP netif mocks
typedef void* esp_netif_t;
esp_netif_t* esp_netif_get_handle_from_ifkey(const char* key);
esp_err_t esp_netif_napt_enable(esp_netif_t* netif);
const char* esp_err_to_name(esp_err_t err);

// LWIP mocks
#define LOCK_TCPIP_CORE()
#define UNLOCK_TCPIP_CORE()
void ip_napt_enable(uint32_t addr, int enable);

} // namespace test_mocks

// Make types and WiFi available globally
using test_mocks::esp_err_t;
using test_mocks::wifi_promiscuous_pkt_type_t;
using test_mocks::wifi_mode_t;
using test_mocks::wifi_pkt_rx_ctrl_t;
using test_mocks::wifi_promiscuous_pkt_t;
using test_mocks::wifi_promiscuous_filter_t;
using test_mocks::wifi_sta_info_t;
using test_mocks::wifi_sta_list_t;
using test_mocks::WiFi;

#endif // ESP32
