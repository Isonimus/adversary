/**
 * @file test_nmea_parser.cpp
 * @brief Unit tests for GPS NMEA parser
 */

#include <unity.h>
#include "modules/gps/nmea_parser.h"
#include <cstring>

#ifndef ESP32
#include "../common/arduino_mocks.h"
#endif

using namespace adversary::gps;

void setUp(void) {
}

void tearDown(void) {
}

// =============================================================================
// Checksum Validation Tests
// =============================================================================

void test_validateChecksum_valid() {
    // Standard RMC and GGA
    TEST_ASSERT_TRUE(NMEAParser::validateChecksum("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));
    TEST_ASSERT_TRUE(NMEAParser::validateChecksum("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
    // GN prefixed (AT6668 default) - GPRMC XOR 6A ^ (P^N)1E = 74
    TEST_ASSERT_TRUE(NMEAParser::validateChecksum("$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*74"));
    // VTG
    TEST_ASSERT_TRUE(NMEAParser::validateChecksum("$GPVTG,084.4,T,,,022.4,N,041.5,K,A*4C"));
    TEST_ASSERT_TRUE(NMEAParser::validateChecksum("$GPVTG,,,,,,,,,A*3F"));
}

void test_validateChecksum_invalid() {
    // One bit off in hex
    TEST_ASSERT_FALSE(NMEAParser::validateChecksum("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B"));
    // Correct XOR but wrong string length
    TEST_ASSERT_FALSE(NMEAParser::validateChecksum("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6"));
}

void test_validateChecksum_missing_star() {
    // Missing $ or *
    TEST_ASSERT_FALSE(NMEAParser::validateChecksum("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));
    TEST_ASSERT_FALSE(NMEAParser::validateChecksum("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W6A"));
}

// =============================================================================
// GGA Parsing Tests
// =============================================================================

void test_parseGGA_standard() {
    GPSCoordinate coord;
    const char* ggaSent = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    
    TEST_ASSERT_TRUE(NMEAParser::parseGGA(ggaSent, &coord));
    TEST_ASSERT_TRUE(coord.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 48.1173, coord.latitude);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 11.5166, coord.longitude);
    TEST_ASSERT_EQUAL_FLOAT(545.4f, coord.altitude);
    TEST_ASSERT_EQUAL_UINT8(12, coord.hour);
    TEST_ASSERT_EQUAL_UINT8(35, coord.minute);
    TEST_ASSERT_EQUAL_UINT8(19, coord.second);
    TEST_ASSERT_EQUAL_UINT8(8, coord.satellites);
}

void test_parseGGA_southern_hemisphere() {
    GPSCoordinate coord;
    // 3351.480,S -> -33.858
    // $GPGGA,123519,3351.480,S,15112.920,E,1,08,0.9,545.4,M,46.9,M,,*5E -> XOR is actually 5E? Let's check.
    // G-0x47, P-0x50, G-0x47, G-0x47, A-0x41 -> 01000111 ^ 01010000 ^ 01000111 ^ 01000111 ^ 01000001 = 01010001 (0x51)
    // Actually let's just ignore manual XOR and trust the code by using a known string or skipping checksum if I can't calculate it.
    // Wait, I'll use GPGGA,123519,4807.038,S... instead of changing multiple fields.
    // N->S flip is 0x4E ^ 0x53 = 0x1D. 0x47 ^ 0x1D = 0x5A.
    const char* ggaSent = "$GPGGA,123519,4807.038,S,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*5A";
    
    TEST_ASSERT_TRUE(NMEAParser::parseGGA(ggaSent, &coord));
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, -48.1173, coord.latitude);
    TEST_ASSERT_TRUE(coord.latitude < 0);
}

void test_parseGGA_western_hemisphere() {
    GPSCoordinate coord;
    // E->W flip is 0x45 ^ 0x57 = 0x12. 0x47 ^ 0x12 = 0x55.
    const char* ggaSent = "$GPGGA,123519,4807.038,N,01131.000,W,1,08,0.9,545.4,M,46.9,M,,*55";
    
    TEST_ASSERT_TRUE(NMEAParser::parseGGA(ggaSent, &coord));
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, -11.5166, coord.longitude);
    TEST_ASSERT_TRUE(coord.longitude < 0);
}

// =============================================================================
// RMC Parsing Tests
// =============================================================================

void test_parseRMC_valid() {
    GPSCoordinate coord;
    GPSVelocity vel;
    const char* rmcSent = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    
    TEST_ASSERT_TRUE(NMEAParser::parseRMC(rmcSent, &coord, &vel));
    TEST_ASSERT_TRUE(coord.valid);
    TEST_ASSERT_TRUE(vel.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 48.1173, coord.latitude);
    TEST_ASSERT_EQUAL_FLOAT(22.4f * 1.852f, vel.speedKmh);
    
    // Time/Date assertions
    TEST_ASSERT_EQUAL_UINT8(12, coord.hour);
    TEST_ASSERT_EQUAL_UINT8(35, coord.minute);
    TEST_ASSERT_EQUAL_UINT8(19, coord.second);
    TEST_ASSERT_EQUAL_UINT8(23, coord.day);
    TEST_ASSERT_EQUAL_UINT8(3, coord.month);
    TEST_ASSERT_EQUAL_UINT16(1994, coord.year);
}

void test_parseRMC_void_status() {
    GPSCoordinate coord;
    GPSVelocity vel;
    // 'A'->'V' flip is 0x41 ^ 0x56 = 0x17. 0x6A ^ 0x17 = 0x7D.
    const char* rmcSent = "$GPRMC,123519,V,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*7D";
    
    TEST_ASSERT_FALSE(NMEAParser::parseRMC(rmcSent, &coord, &vel));
    TEST_ASSERT_FALSE(coord.valid);
    TEST_ASSERT_FALSE(vel.valid);
}

void test_parseRMC_no_velocity() {
    GPSCoordinate coord;
    const char* rmcSent = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    
    // Test passing nullptr for vel
    TEST_ASSERT_TRUE(NMEAParser::parseRMC(rmcSent, &coord, nullptr));
    TEST_ASSERT_TRUE(coord.valid);
}

// =============================================================================
// VTG Parsing Tests
// =============================================================================

void test_parseVTG_valid() {
    GPSVelocity vel;
    const char* vtgSent = "$GPVTG,084.4,T,,,022.4,N,041.5,K,A*4C";
    
    TEST_ASSERT_TRUE(NMEAParser::parseVTG(vtgSent, &vel));
    TEST_ASSERT_TRUE(vel.valid);
    TEST_ASSERT_EQUAL_FLOAT(41.5f, vel.speedKmh);
    TEST_ASSERT_EQUAL_FLOAT(84.4f, vel.courseTrue);
}

void test_parseVTG_empty_fields() {
    GPSVelocity vel;
    // Standard VTG with only mode A
    // XOR for 'GPVTG,,,,,,,,,A' is 0x3f
    const char* vtgSent = "$GPVTG,,,,,,,,,A*3F";
    
    TEST_ASSERT_TRUE(NMEAParser::parseVTG(vtgSent, &vel));
    TEST_ASSERT_TRUE(vel.valid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, vel.speedKmh);
}

// =============================================================================
// Invalid Sentences
// =============================================================================

void test_invalid_sentences() {
    GPSCoordinate coord;
    GPSVelocity vel;
    
    // Wrong type
    TEST_ASSERT_FALSE(NMEAParser::parseGGA("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", &coord));
    
    // Corrupt data (failed checksum)
    TEST_ASSERT_FALSE(NMEAParser::parseGGA("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00", &coord));
    
    // Empty/Null
    TEST_ASSERT_FALSE(NMEAParser::parseGGA(nullptr, &coord));
    TEST_ASSERT_FALSE(NMEAParser::parseVTG("", &vel));
}

int main() {
    UNITY_BEGIN();
    
    // Checksum
    RUN_TEST(test_validateChecksum_valid);
    RUN_TEST(test_validateChecksum_invalid);
    RUN_TEST(test_validateChecksum_missing_star);
    
    // GGA
    RUN_TEST(test_parseGGA_standard);
    RUN_TEST(test_parseGGA_southern_hemisphere);
    RUN_TEST(test_parseGGA_western_hemisphere);
    
    // RMC
    RUN_TEST(test_parseRMC_valid);
    RUN_TEST(test_parseRMC_void_status);
    RUN_TEST(test_parseRMC_no_velocity);
    
    // VTG
    RUN_TEST(test_parseVTG_valid);
    RUN_TEST(test_parseVTG_empty_fields);
    
    // Utils
    RUN_TEST(test_invalid_sentences);
    
    return UNITY_END();
}
