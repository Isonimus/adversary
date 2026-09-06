/**
 * @file NimBLEDevice.h
 * @brief Minimal NimBLE mock for native testing
 * 
 * Stubs the NimBLE types used by ble_spanner.h so that
 * test_ble_attacks can compile on native (it only tests enums/config structs).
 */

#pragma once

#ifndef ESP32

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Forward declarations / stubs for NimBLE types used by ble_spanner.h

class NimBLEUUID {
public:
    NimBLEUUID(uint16_t uuid) : m_uuid16(uuid), m_is16(true) {}
    bool operator==(const NimBLEUUID& other) const {
        return m_is16 == other.m_is16 && m_uuid16 == other.m_uuid16;
    }
private:
    uint16_t m_uuid16;
    bool m_is16;
};

class NimBLEServer;
class NimBLEAdvertising;
class NimBLECharacteristic;
class NimBLEService;
class NimBLEDescriptor;
class NimBLEAdvertisedDevice;

class NimBLEAddress {
public:
    NimBLEAddress() { memset(m_address, 0, 6); }
    NimBLEAddress(const uint8_t* addr) { memcpy(m_address, addr, 6); }
    NimBLEAddress(const std::string& addr) { memset(m_address, 0, 6); }
    std::string toString() const { return "00:00:00:00:00:00"; }
    void* getNative() const { return (void*)m_address; }
private:
    uint8_t m_address[6];
};

class NimBLEAdvertisedDeviceCallbacks {
public:
    virtual ~NimBLEAdvertisedDeviceCallbacks() {}
    virtual void onResult(NimBLEAdvertisedDevice* advertisedDevice) {}
};

class NimBLEAdvertisedDevice {
public:
    NimBLEAddress getAddress() { return m_address; }
    void setAddress(const NimBLEAddress& addr) { m_address = addr; }
    std::string getName() { return ""; }
    int getRSSI() { return m_rssi; }
    void setRSSI(int rssi) { m_rssi = rssi; }
    bool haveManufacturerData() { return !m_manufacturerData.empty(); }
    std::string getManufacturerData() { return m_manufacturerData; }
    void setManufacturerData(const std::string& data) { m_manufacturerData = data; }
    bool haveServiceUUID() { return !m_serviceUUIDs.empty(); }
    bool isAdvertisingService(const NimBLEUUID& uuid) {
        for (const auto& s : m_serviceUUIDs) if (s == uuid) return true;
        return false;
    }
    void addServiceUUID(const NimBLEUUID& uuid) { m_serviceUUIDs.push_back(uuid); }
    bool haveServiceData() { return !m_serviceData.empty(); }
    int getServiceDataCount() { return m_serviceData.size(); }
    NimBLEUUID getServiceDataUUID(int i) { return m_serviceData[i].first; }
    std::string getServiceData(int i) { return m_serviceData[i].second; }
    std::string getServiceData(const NimBLEUUID& uuid) {
        for (const auto& d : m_serviceData) if (d.first == uuid) return d.second;
        return "";
    }
    void addServiceData(const NimBLEUUID& uuid, const std::string& data) {
        m_serviceData.push_back({uuid, data});
    }

private:
    NimBLEAddress m_address;
    int m_rssi = -100;
    std::string m_manufacturerData;
    std::vector<NimBLEUUID> m_serviceUUIDs;
    std::vector<std::pair<NimBLEUUID, std::string>> m_serviceData;
};

class NimBLEServerCallbacks {
public:
    virtual ~NimBLEServerCallbacks() {}
    virtual void onConnect(NimBLEServer* pServer) {}
    virtual void onDisconnect(NimBLEServer* pServer) {}
};

class NimBLEAdvertisementData {
public:
    void setFlags(uint8_t f) {}
    void setManufacturerData(const std::string& data) {}
    void setName(const std::string& name) {}
    void setCompleteServices(uint16_t uuid) {}
    void addData(const std::string& data) {}
    std::string getPayload() const { return ""; }
};

class NimBLEAdvertising {
public:
    void setAdvertisementData(NimBLEAdvertisementData& data) {}
    void setScanResponseData(NimBLEAdvertisementData& data) {}
    bool start(uint32_t duration = 0) { return true; }
    bool stop() { return true; }
    void reset() {}
};

class NimBLECharacteristic {
public:
    void setValue(const uint8_t* data, size_t len) {}
    void notify() {}
};

class NimBLEDescriptor {};

class NimBLEService {
public:
    NimBLECharacteristic* createCharacteristic(uint16_t uuid, uint32_t props) { return nullptr; }
};

class NimBLEServer {
public:
    NimBLEService* createService(uint16_t uuid) { return nullptr; }
    NimBLEAdvertising* getAdvertising() { return nullptr; }
    void setCallbacks(NimBLEServerCallbacks* cb) {}
    void startAdvertising() {}
};

class NimBLEDevice {
public:
    static void init(const std::string& name) {}
    static void deinit(bool clearAll = false) {}
    static NimBLEServer* createServer() { return nullptr; }
    static NimBLEAdvertising* getAdvertising() { static NimBLEAdvertising adv; return &adv; }
    static void setSecurityAuth(uint8_t auth) {}
    static void setPower(int power) {}
    static std::string getAddress() { return "AA:BB:CC:DD:EE:FF"; }
};

#endif // ESP32
