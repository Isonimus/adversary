/**
 * @file test_rolling_status.cpp
 * @brief Unit tests for the autohunt rolling-status overlap policy (ADR-0164).
 *
 * The RollingStatus buffer keeps the most recent engine event for inline
 * rendering after the "Xs remaining" countdown. Policy (slice-0165):
 *   - higher priority preempts the current string immediately,
 *   - same-or-lower priority waits for the current dwell to expire,
 *   - mid-dwell same-or-lower messages are dropped (no queue),
 *   - dwell is 2000 ms for priorities 1-3 and 4000 ms for priority 4,
 *   - the string buffer is 48 B, so a long input truncates with NUL.
 *
 * The clock is hand-driven here: `now` is a plain `uint32_t` passed into every
 * emit/visible call, not `millis()`. Same clock-injection shape as
 * test_autohunt_backoff.
 */

#include <unity.h>
#include <cstring>

#include "ui/components/rolling_status.h"

using adversary::ui::RollingStatus;
using adversary::ui::STATUS_PRIO_IDLE;
using adversary::ui::STATUS_PRIO_LOW;
using adversary::ui::STATUS_PRIO_MED;
using adversary::ui::STATUS_PRIO_HIGH;
using adversary::ui::STATUS_DWELL_LOW_MS;
using adversary::ui::STATUS_DWELL_HIGH_MS;
using adversary::ui::STATUS_MSG_CAP;

void setUp() {}
void tearDown() {}

// ---------- Dwell ----------

void test_single_message_dwells_for_full_period(void) {
    RollingStatus s;
    TEST_ASSERT_TRUE(s.emit("Client 02:AD:5A:11:ED:01", STATUS_PRIO_LOW, 0));
    TEST_ASSERT_TRUE(s.visible(0));
    TEST_ASSERT_TRUE(s.visible(1999));
    TEST_ASSERT_FALSE(s.visible(2000));
    TEST_ASSERT_FALSE(s.visible(2001));
}

// ---------- Priority preemption ----------

void test_higher_priority_preempts_mid_dwell(void) {
    RollingStatus s;
    s.emit("QoS-Null to 1 client", STATUS_PRIO_LOW, 0);
    TEST_ASSERT_EQUAL_STRING("QoS-Null to 1 client", s.text());
    TEST_ASSERT_EQUAL_UINT32(2000, s.expiryMs);

    // At t=500ms (1500ms of dwell left), a prio-4 arrives.
    TEST_ASSERT_TRUE(s.emit("Handshake captured", STATUS_PRIO_HIGH, 500));
    TEST_ASSERT_EQUAL_STRING("Handshake captured", s.text());
    // New dwell is the HIGH dwell, anchored at the preemption moment.
    TEST_ASSERT_EQUAL_UINT32(500 + STATUS_DWELL_HIGH_MS, s.expiryMs);
}

// ---------- Same-priority drop + replace ----------

void test_same_priority_drops_mid_dwell(void) {
    RollingStatus s;
    s.emit("Client A", STATUS_PRIO_LOW, 0);
    // Same priority, still in dwell: dropped.
    TEST_ASSERT_FALSE(s.emit("Client B", STATUS_PRIO_LOW, 500));
    TEST_ASSERT_EQUAL_STRING("Client A", s.text());
    TEST_ASSERT_EQUAL_UINT32(2000, s.expiryMs);
}

void test_same_priority_replaces_after_dwell(void) {
    RollingStatus s;
    s.emit("Client A", STATUS_PRIO_LOW, 0);
    // t=2500ms is past the 2000ms dwell: replacement accepted.
    TEST_ASSERT_TRUE(s.emit("Client B", STATUS_PRIO_LOW, 2500));
    TEST_ASSERT_EQUAL_STRING("Client B", s.text());
    TEST_ASSERT_EQUAL_UINT32(2500 + STATUS_DWELL_LOW_MS, s.expiryMs);
}

// ---------- Lower priority drop ----------

void test_lower_priority_drops_mid_dwell(void) {
    RollingStatus s;
    s.emit("Handshake captured", STATUS_PRIO_HIGH, 0);
    // Prio-2 arrives mid-dwell against a prio-4 that owns 4000 ms: dropped.
    TEST_ASSERT_FALSE(s.emit("Client foo", STATUS_PRIO_LOW, 500));
    TEST_ASSERT_EQUAL_STRING("Handshake captured", s.text());
    // Original expiry survives.
    TEST_ASSERT_EQUAL_UINT32(STATUS_DWELL_HIGH_MS, s.expiryMs);
    TEST_ASSERT_TRUE(s.visible(3999));
    TEST_ASSERT_FALSE(s.visible(4000));
}

// ---------- Buffer truncation ----------

void test_truncation_at_buffer_cap(void) {
    RollingStatus s;
    // 60-char string; buffer caps at 48 (47 chars + NUL).
    const char* longStr =
        "This string is quite a bit longer than the buffer cap XXX";
    s.emit(longStr, STATUS_PRIO_MED, 0);
    TEST_ASSERT_EQUAL_size_t(STATUS_MSG_CAP - 1, strlen(s.text()));
    // The leading characters survive; only the tail is clipped.
    TEST_ASSERT_EQUAL_STRING_LEN(longStr, s.text(), STATUS_MSG_CAP - 1);
}

// ---------- Idle fallback (prio 1) ----------

void test_idle_prio1_does_not_preempt_prio2(void) {
    // Scenario: prio-2 is dwelling; a prio-1 idle emit arrives. It must be
    // dropped (same-or-lower during dwell). This is the ADR-0164 contract for
    // the "Waiting for EAPOL M3..." fallback: it fills gaps, never preempts.
    RollingStatus s;
    s.emit("Client A", STATUS_PRIO_LOW, 0);
    TEST_ASSERT_FALSE(s.emit("Waiting for EAPOL M3...", STATUS_PRIO_IDLE, 500));
    TEST_ASSERT_EQUAL_STRING("Client A", s.text());
}

void test_idle_prio1_fills_after_expiry(void) {
    RollingStatus s;
    s.emit("Client A", STATUS_PRIO_LOW, 0);
    // After expiry the prio-1 idle fallback can take the slot.
    TEST_ASSERT_TRUE(s.emit("Waiting for EAPOL M3...", STATUS_PRIO_IDLE, 2500));
    TEST_ASSERT_EQUAL_STRING("Waiting for EAPOL M3...", s.text());
    TEST_ASSERT_EQUAL_UINT32(2500 + STATUS_DWELL_LOW_MS, s.expiryMs);
}

// ---------- Reset ----------

void test_reset_clears_buffer_and_prio(void) {
    RollingStatus s;
    s.emit("Client A", STATUS_PRIO_LOW, 0);
    s.reset();
    TEST_ASSERT_EQUAL_UINT8('\0', s.msg[0]);
    TEST_ASSERT_EQUAL_UINT8(0, s.prio);
    TEST_ASSERT_FALSE(s.visible(100));
    // After reset, a prio-1 can land immediately.
    TEST_ASSERT_TRUE(s.emit("Waiting for EAPOL M3...", STATUS_PRIO_IDLE, 100));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_single_message_dwells_for_full_period);
    RUN_TEST(test_higher_priority_preempts_mid_dwell);
    RUN_TEST(test_same_priority_drops_mid_dwell);
    RUN_TEST(test_same_priority_replaces_after_dwell);
    RUN_TEST(test_lower_priority_drops_mid_dwell);
    RUN_TEST(test_truncation_at_buffer_cap);
    RUN_TEST(test_idle_prio1_does_not_preempt_prio2);
    RUN_TEST(test_idle_prio1_fills_after_expiry);
    RUN_TEST(test_reset_clears_buffer_and_prio);
    return UNITY_END();
}
