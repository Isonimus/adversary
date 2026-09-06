/**
 * @file test_notification_manager.cpp
 * @brief Unit tests for the Notification Manager module
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

// =============================================================================
// Local type definitions for native testing (avoid ESP32 dependencies)
// =============================================================================

namespace adversary {

/**
 * @brief Notification pattern definition
 */
struct NotificationPattern {
    const uint16_t* frequencies;
    const uint16_t* durations;
    uint8_t noteCount;
    uint32_t ledColor;
    uint8_t ledFlashes;
    uint16_t ledOnMs;
    uint16_t ledOffMs;
};

/**
 * @brief Notification event types
 */
enum class NotificationType : uint8_t {
    EAPOL_PACKET = 0,
    HANDSHAKE_COMPLETE = 1,
    PMKID_CAPTURED = 2,
    ATTACK_STARTED = 3,
    ATTACK_STOPPED = 4,
    ERROR = 5,
    WARNING = 6,
    CREDENTIAL_CAPTURED = 7,
    TARGET_WHITELISTED = 8,
    SCAN_COMPLETE = 9
};

/**
 * @brief Notification priority levels
 */
enum class NotificationPriority : uint8_t {
    PRIORITY_LOW = 0,
    PRIORITY_MEDIUM = 1,
    PRIORITY_HIGH = 2
};

// Predefined patterns
inline const uint16_t EAPOL_FREQS[] = {800};
inline const uint16_t EAPOL_DURS[] = {30};
inline const NotificationPattern PATTERN_EAPOL = {
    EAPOL_FREQS, EAPOL_DURS, 1,
    0xFF6600, 1, 30, 0
};

inline const uint16_t HS_FREQS[] = {523, 659, 784};
inline const uint16_t HS_DURS[] = {80, 80, 120};
inline const NotificationPattern PATTERN_HANDSHAKE = {
    HS_FREQS, HS_DURS, 3,
    0x00FF00, 2, 100, 100
};

inline const uint16_t PMKID_FREQS[] = {1000, 0, 1000};
inline const uint16_t PMKID_DURS[] = {40, 20, 40};
inline const NotificationPattern PATTERN_PMKID = {
    PMKID_FREQS, PMKID_DURS, 3,
    0x0066FF, 1, 80, 0
};

inline const uint16_t ERROR_FREQS[] = {200, 0, 200};
inline const uint16_t ERROR_DURS[] = {100, 50, 100};
inline const NotificationPattern PATTERN_ERROR = {
    ERROR_FREQS, ERROR_DURS, 3,
    0xFF0000, 3, 50, 50
};

inline const uint16_t WARN_FREQS[] = {300};
inline const uint16_t WARN_DURS[] = {150};
inline const NotificationPattern PATTERN_WARNING = {
    WARN_FREQS, WARN_DURS, 1,
    0xFFFF00, 1, 100, 0
};

inline const uint16_t CRED_FREQS[] = {523, 659, 784, 1047};
inline const uint16_t CRED_DURS[] = {60, 60, 60, 150};
inline const NotificationPattern PATTERN_CREDENTIAL = {
    CRED_FREQS, CRED_DURS, 4,
    0x00FFFF, 3, 80, 80
};

} // namespace adversary

using namespace adversary;

// =============================================================================
// NotificationType Enum Tests
// =============================================================================

void test_notification_type_eapol_value() {
    TEST_ASSERT_EQUAL(0, static_cast<uint8_t>(NotificationType::EAPOL_PACKET));
}

void test_notification_type_handshake_complete_value() {
    TEST_ASSERT_EQUAL(1, static_cast<uint8_t>(NotificationType::HANDSHAKE_COMPLETE));
}

void test_notification_type_pmkid_value() {
    TEST_ASSERT_EQUAL(2, static_cast<uint8_t>(NotificationType::PMKID_CAPTURED));
}

void test_notification_type_attack_started_value() {
    TEST_ASSERT_EQUAL(3, static_cast<uint8_t>(NotificationType::ATTACK_STARTED));
}

void test_notification_type_attack_stopped_value() {
    TEST_ASSERT_EQUAL(4, static_cast<uint8_t>(NotificationType::ATTACK_STOPPED));
}

void test_notification_type_error_value() {
    TEST_ASSERT_EQUAL(5, static_cast<uint8_t>(NotificationType::ERROR));
}

void test_notification_type_warning_value() {
    TEST_ASSERT_EQUAL(6, static_cast<uint8_t>(NotificationType::WARNING));
}

void test_notification_type_credential_value() {
    TEST_ASSERT_EQUAL(7, static_cast<uint8_t>(NotificationType::CREDENTIAL_CAPTURED));
}

void test_notification_type_whitelisted_value() {
    TEST_ASSERT_EQUAL(8, static_cast<uint8_t>(NotificationType::TARGET_WHITELISTED));
}

void test_notification_type_scan_complete_value() {
    TEST_ASSERT_EQUAL(9, static_cast<uint8_t>(NotificationType::SCAN_COMPLETE));
}

// =============================================================================
// NotificationPriority Enum Tests
// =============================================================================

void test_priority_low_value() {
    TEST_ASSERT_EQUAL(0, static_cast<uint8_t>(NotificationPriority::PRIORITY_LOW));
}

void test_priority_medium_value() {
    TEST_ASSERT_EQUAL(1, static_cast<uint8_t>(NotificationPriority::PRIORITY_MEDIUM));
}

void test_priority_high_value() {
    TEST_ASSERT_EQUAL(2, static_cast<uint8_t>(NotificationPriority::PRIORITY_HIGH));
}

// =============================================================================
// NotificationPattern Tests
// =============================================================================

void test_pattern_eapol_note_count() {
    TEST_ASSERT_EQUAL(1, PATTERN_EAPOL.noteCount);
}

void test_pattern_eapol_frequency() {
    TEST_ASSERT_EQUAL(800, PATTERN_EAPOL.frequencies[0]);
}

void test_pattern_eapol_duration() {
    TEST_ASSERT_EQUAL(30, PATTERN_EAPOL.durations[0]);
}

void test_pattern_eapol_led_color() {
    TEST_ASSERT_EQUAL(0xFF6600, PATTERN_EAPOL.ledColor);
}

void test_pattern_eapol_led_flashes() {
    TEST_ASSERT_EQUAL(1, PATTERN_EAPOL.ledFlashes);
}

void test_pattern_handshake_note_count() {
    TEST_ASSERT_EQUAL(3, PATTERN_HANDSHAKE.noteCount);
}

void test_pattern_handshake_frequencies() {
    TEST_ASSERT_EQUAL(523, PATTERN_HANDSHAKE.frequencies[0]);  // C5
    TEST_ASSERT_EQUAL(659, PATTERN_HANDSHAKE.frequencies[1]);  // E5
    TEST_ASSERT_EQUAL(784, PATTERN_HANDSHAKE.frequencies[2]);  // G5
}

void test_pattern_handshake_led_color_green() {
    TEST_ASSERT_EQUAL(0x00FF00, PATTERN_HANDSHAKE.ledColor);
}

void test_pattern_handshake_led_flashes() {
    TEST_ASSERT_EQUAL(2, PATTERN_HANDSHAKE.ledFlashes);
}

void test_pattern_pmkid_note_count() {
    TEST_ASSERT_EQUAL(3, PATTERN_PMKID.noteCount);
}

void test_pattern_pmkid_has_rest() {
    // Second note is rest (freq = 0)
    TEST_ASSERT_EQUAL(0, PATTERN_PMKID.frequencies[1]);
}

void test_pattern_pmkid_led_color_blue() {
    TEST_ASSERT_EQUAL(0x0066FF, PATTERN_PMKID.ledColor);
}

void test_pattern_error_note_count() {
    TEST_ASSERT_EQUAL(3, PATTERN_ERROR.noteCount);
}

void test_pattern_error_led_color_red() {
    TEST_ASSERT_EQUAL(0xFF0000, PATTERN_ERROR.ledColor);
}

void test_pattern_error_led_rapid_flash() {
    TEST_ASSERT_EQUAL(3, PATTERN_ERROR.ledFlashes);
    TEST_ASSERT_EQUAL(50, PATTERN_ERROR.ledOnMs);
    TEST_ASSERT_EQUAL(50, PATTERN_ERROR.ledOffMs);
}

void test_pattern_warning_single_beep() {
    TEST_ASSERT_EQUAL(1, PATTERN_WARNING.noteCount);
    TEST_ASSERT_EQUAL(300, PATTERN_WARNING.frequencies[0]);
}

void test_pattern_warning_led_color_yellow() {
    TEST_ASSERT_EQUAL(0xFFFF00, PATTERN_WARNING.ledColor);
}

void test_pattern_credential_note_count() {
    TEST_ASSERT_EQUAL(4, PATTERN_CREDENTIAL.noteCount);
}

void test_pattern_credential_ascending() {
    // Verify ascending melody
    TEST_ASSERT_GREATER_THAN(PATTERN_CREDENTIAL.frequencies[0], PATTERN_CREDENTIAL.frequencies[1]);
    TEST_ASSERT_GREATER_THAN(PATTERN_CREDENTIAL.frequencies[1], PATTERN_CREDENTIAL.frequencies[2]);
    TEST_ASSERT_GREATER_THAN(PATTERN_CREDENTIAL.frequencies[2], PATTERN_CREDENTIAL.frequencies[3]);
}

void test_pattern_credential_led_color_cyan() {
    TEST_ASSERT_EQUAL(0x00FFFF, PATTERN_CREDENTIAL.ledColor);
}

void test_pattern_credential_led_triple_flash() {
    TEST_ASSERT_EQUAL(3, PATTERN_CREDENTIAL.ledFlashes);
}

// =============================================================================
// LED Color Extraction Tests
// =============================================================================

void test_led_color_extract_red() {
    uint32_t color = 0xFF0000;
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;
    
    TEST_ASSERT_EQUAL(255, red);
    TEST_ASSERT_EQUAL(0, green);
    TEST_ASSERT_EQUAL(0, blue);
}

void test_led_color_extract_green() {
    uint32_t color = 0x00FF00;
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;
    
    TEST_ASSERT_EQUAL(0, red);
    TEST_ASSERT_EQUAL(255, green);
    TEST_ASSERT_EQUAL(0, blue);
}

void test_led_color_extract_blue() {
    uint32_t color = 0x0000FF;
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;
    
    TEST_ASSERT_EQUAL(0, red);
    TEST_ASSERT_EQUAL(0, green);
    TEST_ASSERT_EQUAL(255, blue);
}

void test_led_color_extract_orange() {
    uint32_t color = 0xFF6600;  // EAPOL color
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;
    
    TEST_ASSERT_EQUAL(255, red);
    TEST_ASSERT_EQUAL(102, green);
    TEST_ASSERT_EQUAL(0, blue);
}

// =============================================================================
// Test Runner
// =============================================================================

void setUp() {}
void tearDown() {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    
    // NotificationType enum tests
    RUN_TEST(test_notification_type_eapol_value);
    RUN_TEST(test_notification_type_handshake_complete_value);
    RUN_TEST(test_notification_type_pmkid_value);
    RUN_TEST(test_notification_type_attack_started_value);
    RUN_TEST(test_notification_type_attack_stopped_value);
    RUN_TEST(test_notification_type_error_value);
    RUN_TEST(test_notification_type_warning_value);
    RUN_TEST(test_notification_type_credential_value);
    RUN_TEST(test_notification_type_whitelisted_value);
    RUN_TEST(test_notification_type_scan_complete_value);
    
    // NotificationPriority enum tests
    RUN_TEST(test_priority_low_value);
    RUN_TEST(test_priority_medium_value);
    RUN_TEST(test_priority_high_value);
    
    // EAPOL pattern tests
    RUN_TEST(test_pattern_eapol_note_count);
    RUN_TEST(test_pattern_eapol_frequency);
    RUN_TEST(test_pattern_eapol_duration);
    RUN_TEST(test_pattern_eapol_led_color);
    RUN_TEST(test_pattern_eapol_led_flashes);
    
    // Handshake pattern tests
    RUN_TEST(test_pattern_handshake_note_count);
    RUN_TEST(test_pattern_handshake_frequencies);
    RUN_TEST(test_pattern_handshake_led_color_green);
    RUN_TEST(test_pattern_handshake_led_flashes);
    
    // PMKID pattern tests
    RUN_TEST(test_pattern_pmkid_note_count);
    RUN_TEST(test_pattern_pmkid_has_rest);
    RUN_TEST(test_pattern_pmkid_led_color_blue);
    
    // Error pattern tests
    RUN_TEST(test_pattern_error_note_count);
    RUN_TEST(test_pattern_error_led_color_red);
    RUN_TEST(test_pattern_error_led_rapid_flash);
    
    // Warning pattern tests
    RUN_TEST(test_pattern_warning_single_beep);
    RUN_TEST(test_pattern_warning_led_color_yellow);
    
    // Credential pattern tests
    RUN_TEST(test_pattern_credential_note_count);
    RUN_TEST(test_pattern_credential_ascending);
    RUN_TEST(test_pattern_credential_led_color_cyan);
    RUN_TEST(test_pattern_credential_led_triple_flash);
    
    // LED color extraction tests
    RUN_TEST(test_led_color_extract_red);
    RUN_TEST(test_led_color_extract_green);
    RUN_TEST(test_led_color_extract_blue);
    RUN_TEST(test_led_color_extract_orange);
    
    return UNITY_END();
}
