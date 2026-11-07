/**
 * @file test_sync_recap_dialog.cpp
 * @brief Logic regression for the post-sync recap (ADR-0142 / slice-0143).
 *
 * The recap is the only surface that survives a bulk sync (the canvas is purged for the
 * whole run), so its tallying, failure-list cap + overflow count, scroll clamp, and the
 * stopped/complete + dismiss behaviour are pinned here. The render(Canvas&) template is
 * device-only, covered by build + on-device review (same split as test_confirm_dialog).
 */

#include <unity.h>
#include "ui/components/sync_recap_dialog.h"

using adversary::UploadOutcome;
using adversary::ui::SyncRecapDialog;
using Service = adversary::ui::SyncRecapDialog::Service;

// Mirror the component's private constants so the expectations are explicit.
static constexpr int FAILED_MAX = 16;
static constexpr int VISIBLE_FAIL_ROWS = 4;

static int s_dismissals = 0;

void setUp() { s_dismissals = 0; }
void tearDown() {}

static void recordFailures(SyncRecapDialog& d, Service svc, int n) {
    for (int i = 0; i < n; ++i) {
        d.record(svc, UploadOutcome::Failed, "Net", "no .22000 file");
    }
}

void test_fresh_reset_is_empty_and_hidden() {
    SyncRecapDialog d;
    TEST_ASSERT_FALSE(d.isVisible());
    TEST_ASSERT_FALSE(d.stopped());
    TEST_ASSERT_EQUAL(0, d.sent(Service::WpaSec));
    TEST_ASSERT_EQUAL(0, d.already(Service::Pwncrack));
    TEST_ASSERT_EQUAL(0, d.totalFailed());
    TEST_ASSERT_EQUAL(0, d.listedFailures());
    TEST_ASSERT_EQUAL(0, d.overflow());
    TEST_ASSERT_EQUAL(0, d.maxScrollOffset());
}

void test_tallies_split_by_service_and_outcome() {
    SyncRecapDialog d;
    d.record(Service::WpaSec, UploadOutcome::Sent, "A", nullptr);
    d.record(Service::WpaSec, UploadOutcome::Sent, "B", nullptr);
    d.record(Service::WpaSec, UploadOutcome::AlreadyUploaded, "C", nullptr);
    d.record(Service::Pwncrack, UploadOutcome::Sent, "D", nullptr);
    d.record(Service::Pwncrack, UploadOutcome::Failed, "E", "connection failed");

    TEST_ASSERT_EQUAL(2, d.sent(Service::WpaSec));
    TEST_ASSERT_EQUAL(1, d.already(Service::WpaSec));
    TEST_ASSERT_EQUAL(0, d.failed(Service::WpaSec));
    TEST_ASSERT_EQUAL(1, d.sent(Service::Pwncrack));
    TEST_ASSERT_EQUAL(1, d.failed(Service::Pwncrack));
    TEST_ASSERT_EQUAL(1, d.totalFailed());
    TEST_ASSERT_EQUAL(1, d.listedFailures());
    TEST_ASSERT_EQUAL(0, d.overflow());
}

void test_failed_list_caps_and_counts_overflow() {
    SyncRecapDialog d;
    const int extra = 5;
    recordFailures(d, Service::Pwncrack, FAILED_MAX + extra);

    // Every failure is tallied, but only FAILED_MAX are stored; the rest are overflow.
    TEST_ASSERT_EQUAL(FAILED_MAX + extra, d.totalFailed());
    TEST_ASSERT_EQUAL(FAILED_MAX + extra, d.failed(Service::Pwncrack));
    TEST_ASSERT_EQUAL(FAILED_MAX, d.listedFailures());
    TEST_ASSERT_EQUAL(extra, d.overflow());
}

void test_reset_clears_everything() {
    SyncRecapDialog d;
    recordFailures(d, Service::WpaSec, 3);
    d.record(Service::WpaSec, UploadOutcome::Sent, "x", nullptr);
    d.setStopped(true);
    d.show();

    d.reset();
    TEST_ASSERT_FALSE(d.isVisible());
    TEST_ASSERT_FALSE(d.stopped());
    TEST_ASSERT_EQUAL(0, d.totalFailed());
    TEST_ASSERT_EQUAL(0, d.sent(Service::WpaSec));
    TEST_ASSERT_EQUAL(0, d.scrollOffset());
}

void test_scroll_clamps_within_hidden_rows() {
    SyncRecapDialog d;
    recordFailures(d, Service::Pwncrack, 10);  // listed = 10, hidden = 10 - 4 = 6
    TEST_ASSERT_EQUAL(10 - VISIBLE_FAIL_ROWS, d.maxScrollOffset());

    d.show();
    // Up at the top is a no-op.
    TEST_ASSERT_TRUE(d.handleInput(';'));
    TEST_ASSERT_EQUAL(0, d.scrollOffset());

    // Down advances one per press, clamped at maxScrollOffset.
    for (int i = 0; i < 20; ++i) d.handleInput('.');
    TEST_ASSERT_EQUAL(d.maxScrollOffset(), d.scrollOffset());

    // Back up to the top, not past it.
    for (int i = 0; i < 20; ++i) d.handleInput(';');
    TEST_ASSERT_EQUAL(0, d.scrollOffset());
}

void test_no_scroll_when_list_fits() {
    SyncRecapDialog d;
    recordFailures(d, Service::WpaSec, VISIBLE_FAIL_ROWS);  // exactly fills, nothing hidden
    TEST_ASSERT_EQUAL(0, d.maxScrollOffset());
    d.show();
    d.handleInput('.');
    TEST_ASSERT_EQUAL(0, d.scrollOffset());
}

void test_enter_and_esc_dismiss_and_fire_callback() {
    SyncRecapDialog d;
    d.setOnDismiss([]() { ++s_dismissals; });

    d.show();
    TEST_ASSERT_TRUE(d.isVisible());
    TEST_ASSERT_TRUE(d.handleInput('\n'));  // Enter
    TEST_ASSERT_FALSE(d.isVisible());
    TEST_ASSERT_EQUAL(1, s_dismissals);

    d.show();
    TEST_ASSERT_TRUE(d.handleInput(27));    // ESC
    TEST_ASSERT_FALSE(d.isVisible());
    TEST_ASSERT_EQUAL(2, s_dismissals);

    d.show();
    TEST_ASSERT_TRUE(d.handleInput('`'));   // backtick / back
    TEST_ASSERT_FALSE(d.isVisible());
    TEST_ASSERT_EQUAL(3, s_dismissals);
}

void test_other_keys_are_swallowed_not_closing() {
    SyncRecapDialog d;
    d.show();
    // A blocking modal consumes stray keys (returns true) without closing.
    TEST_ASSERT_TRUE(d.handleInput('x'));
    TEST_ASSERT_TRUE(d.isVisible());
}

void test_input_ignored_when_hidden() {
    SyncRecapDialog d;  // not shown
    TEST_ASSERT_FALSE(d.handleInput('\n'));
    TEST_ASSERT_FALSE(d.handleInput('.'));
}

void test_stopped_predicate_tracks_setter() {
    SyncRecapDialog d;
    TEST_ASSERT_FALSE(d.stopped());
    d.setStopped(true);
    TEST_ASSERT_TRUE(d.stopped());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fresh_reset_is_empty_and_hidden);
    RUN_TEST(test_tallies_split_by_service_and_outcome);
    RUN_TEST(test_failed_list_caps_and_counts_overflow);
    RUN_TEST(test_reset_clears_everything);
    RUN_TEST(test_scroll_clamps_within_hidden_rows);
    RUN_TEST(test_no_scroll_when_list_fits);
    RUN_TEST(test_enter_and_esc_dismiss_and_fire_callback);
    RUN_TEST(test_other_keys_are_swallowed_not_closing);
    RUN_TEST(test_input_ignored_when_hidden);
    RUN_TEST(test_stopped_predicate_tracks_setter);
    return UNITY_END();
}
