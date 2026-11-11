/**
 * @file test_crash_report.cpp
 * @brief Regression for the pure crash-log core (ADR-0126, slice-0127).
 *
 * The device shell does the I/O; the two host-testable seams are classifyReset (which
 * resets count as a crash worth logging) and formatCrashEntry (what the log line says,
 * and crucially that an unsurvived RTC snapshot reads "unavailable" rather than stale
 * numbers). Getting either wrong reinstates the original defect: a log that looks
 * authoritative but describes boot-time or garbage heap, not the dying heap.
 */

#include <unity.h>

#include <cstring>

#include "core/crash_report.h"

using adversary::classifyReset;
using adversary::composeCoredumpPath;
using adversary::formatCrashEntry;
using adversary::HeapSnapshot;
using adversary::ResetClassification;
using adversary::ResetReason;

void setUp(void) {}
void tearDown(void) {}

// Every abnormal reset is a crash worth logging; its label is non-empty.
void test_abnormal_resets_are_crashes(void) {
    const ResetReason crashes[] = {
        ResetReason::Panic, ResetReason::IntWdt, ResetReason::TaskWdt,
        ResetReason::OtherWdt, ResetReason::Brownout, ResetReason::ExternalSdio,
        // Abnormal CPU/hardware reset causes that classifyReset must flag as crashes. The
        // stack-canary panic that prompted this does NOT present as any of them on the
        // USB-CDC board (it resets as UsbReset, measured 2026-11-01); mapping them is still
        // correct so crash.log catches these causes when they genuinely occur.
        ResetReason::CpuLockup, ResetReason::PowerGlitch, ResetReason::Efuse,
    };
    for (ResetReason r : crashes) {
        ResetClassification c = classifyReset(r);
        TEST_ASSERT_TRUE(c.isCrash);
        TEST_ASSERT_NOT_NULL(c.name);
        TEST_ASSERT_TRUE(strlen(c.name) > 0);
    }
}

// Clean resets are not crashes, so the device clears its crash streak on them.
void test_clean_resets_are_not_crashes(void) {
    const ResetReason clean[] = {
        ResetReason::PowerOn, ResetReason::Software,
        ResetReason::DeepSleep, ResetReason::Unknown,
        ResetReason::UsbReset, ResetReason::Jtag, ResetReason::ExternalPin,
    };
    for (ResetReason r : clean) {
        ResetClassification c = classifyReset(r);
        TEST_ASSERT_FALSE(c.isCrash);
        TEST_ASSERT_NOT_NULL(c.name);
    }
}

// A valid snapshot prints the recorded reason, count, timestamp, and every heap number.
void test_format_valid_snapshot_includes_numbers(void) {
    char buf[512];
    ResetClassification c = classifyReset(ResetReason::TaskWdt);
    HeapSnapshot s{/*free*/ 40960, /*largest*/ 18000, /*min*/ 32000, /*uptimeMs*/ 123456};

    formatCrashEntry(buf, sizeof(buf), c, /*count*/ 2, /*valid*/ true, s, "2026-10-26 14:03:22 UTC");

    TEST_ASSERT_NOT_NULL(strstr(buf, "count: 2"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "TASK_WDT"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "2026-10-26 14:03:22 UTC"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "123456 ms"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "40960 bytes"));   // free heap at crash
    TEST_ASSERT_NOT_NULL(strstr(buf, "18000 bytes"));   // largest block = fragmentation
    TEST_ASSERT_NOT_NULL(strstr(buf, "32000 bytes"));   // min free
    TEST_ASSERT_NULL(strstr(buf, "unavailable"));
}

// An unsurvived snapshot must read "unavailable" and must NOT emit any heap figure that
// a reader could mistake for crash state. The numbers in the struct are stale, so none
// of them may appear.
void test_format_invalid_snapshot_says_unavailable(void) {
    char buf[512];
    ResetClassification c = classifyReset(ResetReason::Panic);
    HeapSnapshot stale{/*free*/ 149000, /*largest*/ 90000, /*min*/ 149000, /*uptimeMs*/ 1395};

    formatCrashEntry(buf, sizeof(buf), c, /*count*/ 1, /*valid*/ false, stale, "unsynced");

    TEST_ASSERT_NOT_NULL(strstr(buf, "count: 1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "PANIC"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "unsynced"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "unavailable"));
    // None of the stale heap figures leak through.
    TEST_ASSERT_NULL(strstr(buf, "149000"));
    TEST_ASSERT_NULL(strstr(buf, "90000"));
    TEST_ASSERT_NULL(strstr(buf, "1395"));
}

// snprintf semantics: a too-small buffer is still NUL-terminated and the return value is
// the full length the entry would need (so the device could detect truncation).
void test_format_truncation_is_safe(void) {
    char buf[16];
    ResetClassification c = classifyReset(ResetReason::Panic);
    HeapSnapshot s{1, 2, 3, 4};

    int needed = formatCrashEntry(buf, sizeof(buf), c, 1, true, s, "unsynced");

    TEST_ASSERT_EQUAL_CHAR('\0', buf[sizeof(buf) - 1]);
    TEST_ASSERT_TRUE(needed > static_cast<int>(sizeof(buf)));
}

// ADR-0152: the SD-rescue path composes its filename from the reset classification
// name plus the RTC epoch. The composer is pure (string manipulation only), so its
// defects (bad tag parsing, missing fallback when the clock was unsynced) can hide
// here with no device-side symptom until a field dump lands in a bad directory.

// A synced epoch produces a chronological YYYYMMDDTHHMMSSZ stamp and the reason's
// leading identifier as the tag. "WDT (other watchdog)" -> tag "WDT", so the common
// Int-WDT case gets a short, greppable filename.
void test_coredump_path_synced_clock(void) {
    char path[96];
    ResetClassification c = classifyReset(ResetReason::OtherWdt);
    // 2026-10-23 13:55:18 UTC == 1792763718 (one of the three recorded events).
    composeCoredumpPath(path, sizeof(path), /*epoch*/ 1792763718U, /*uptime*/ 0, c.name);

    TEST_ASSERT_EQUAL_STRING("/adversary/coredumps/20261023T135518Z-WDT.elf", path);
}

// Unsynced clock (RTC never got a real time): fall back to an uptime-based name so
// the file is still unique and placed in the same directory. The ADR explicitly
// requires the rescue to work even when the clock was never set.
void test_coredump_path_unsynced_falls_back_to_uptime(void) {
    char path[96];
    ResetClassification c = classifyReset(ResetReason::Panic);
    composeCoredumpPath(path, sizeof(path), /*epoch*/ 0, /*uptime*/ 61000U, c.name);

    // Not an ISO stamp, so a reader does not mistake uptime seconds for a wall clock.
    TEST_ASSERT_NULL(strstr(path, "Z-"));
    TEST_ASSERT_NOT_NULL(strstr(path, "/adversary/coredumps/boot-61000ms-PANIC.elf"));
}

// Null reason name must still produce a well-formed path, so a future caller that
// forgets to pass a classification never writes a half-formed filename to SD.
void test_coredump_path_null_reason_falls_back(void) {
    char path[96];
    composeCoredumpPath(path, sizeof(path), /*epoch*/ 1792763718U, /*uptime*/ 0, nullptr);

    TEST_ASSERT_NOT_NULL(strstr(path, "-UNK.elf"));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_abnormal_resets_are_crashes);
    RUN_TEST(test_clean_resets_are_not_crashes);
    RUN_TEST(test_format_valid_snapshot_includes_numbers);
    RUN_TEST(test_format_invalid_snapshot_says_unavailable);
    RUN_TEST(test_format_truncation_is_safe);
    RUN_TEST(test_coredump_path_synced_clock);
    RUN_TEST(test_coredump_path_unsynced_falls_back_to_uptime);
    RUN_TEST(test_coredump_path_null_reason_falls_back);
    return UNITY_END();
}
