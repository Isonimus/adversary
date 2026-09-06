/**
 * @file esp32_mocks.cpp
 * @brief ESP32 WiFi and networking mock implementations
 */

#include "esp32_mocks.h"

#ifndef ESP32

namespace test_mocks {

// Define global WiFi instance
MockWiFi WiFi;

// ESP WiFi function implementations
esp_err_t esp_wifi_set_promiscuous(bool en) {
    (void)en;
    return ESP_OK;
}

esp_err_t esp_wifi_set_promiscuous_rx_cb(void(*cb)(void*, wifi_promiscuous_pkt_type_t)) {
    (void)cb;
    return ESP_OK;
}

esp_err_t esp_wifi_set_promiscuous_filter(wifi_promiscuous_filter_t* filter) {
    (void)filter;
    return ESP_OK;
}

esp_err_t esp_wifi_get_mode(wifi_mode_t* mode) {
    *mode = WIFI_MODE_STA;
    return ESP_OK;
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode) {
    (void)mode;
    return ESP_OK;
}

esp_err_t esp_wifi_start() {
    return ESP_OK;
}

esp_err_t esp_wifi_80211_tx(int ifx, const void *buffer, int len, bool en_sys_seq) {
    (void)ifx;
    (void)buffer;
    (void)len;
    (void)en_sys_seq;
    return ESP_OK;
}

esp_err_t esp_wifi_set_channel(uint8_t primary, uint8_t second) {
    (void)primary;
    (void)second;
    return ESP_OK;
}

esp_err_t esp_wifi_set_inactive_time(int ifx, uint16_t sec) {
    (void)ifx;
    (void)sec;
    return ESP_OK;
}

esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t* list) {
    list->num = 0;
    return ESP_OK;
}

// ESP netif implementations
esp_netif_t* esp_netif_get_handle_from_ifkey(const char* key) {
    (void)key;
    return (esp_netif_t*)0x1234; // Mock pointer
}

esp_err_t esp_netif_napt_enable(esp_netif_t* netif) {
    (void)netif;
    return ESP_OK;
}

const char* esp_err_to_name(esp_err_t err) {
    (void)err;
    return "ESP_OK";
}

// LWIP implementation
void ip_napt_enable(uint32_t addr, int enable) {
    (void)addr;
    (void)enable;
}

} // namespace test_mocks

#endif // ESP32
