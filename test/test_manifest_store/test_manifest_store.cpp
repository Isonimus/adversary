/**
 * @file test_manifest_store.cpp
 * @brief Unit tests for HandshakeRecord + ManifestStore (storage rework Phase 1)
 *
 * Exercises the flat fixed-size manifest I/O against the host-backed SD mock:
 * create/append/readAt/writeAt, persistence across reopen, CRC integrity, and
 * torn-write reconciliation on open.
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "modules/storage/manifest_store.h"

using namespace adversary;

static const char* kPath = "test_manifest_tmp.bin";
static const char* kTmpPath = "test_manifest_tmp.bin.tmp";

void setUp() {
    ::remove(kPath);
    ::remove(kTmpPath);
}

void tearDown() {
    ::remove(kPath);
    ::remove(kTmpPath);
}

// ---- helpers ---------------------------------------------------------------

static HandshakeRecord makeRecord(const uint8_t bssid[6], const char* ssid,
                                  uint8_t channel = 6, uint32_t pcapSize = 1234) {
    HandshakeRecord r{};
    r.state = static_cast<uint8_t>(RecordState::ACTIVE);
    memcpy(r.bssid, bssid, 6);
    strncpy(r.ssid, ssid, sizeof(r.ssid) - 1);
    r.channel = channel;
    r.type = static_cast<uint8_t>(RecordType::FOURWAY);
    r.flags = RF_HAS_MSG1 | RF_HAS_MSG2;
    r.quality = 50;
    r.rssi = -60;
    r.capturedAt = 1750000000u;
    r.pcapSize = pcapSize;
    r.wpaSecStatus = 0;
    return r;
}

// ---- record struct / crc ---------------------------------------------------

void test_record_is_256_bytes() {
    TEST_ASSERT_EQUAL_UINT32(256, (uint32_t)sizeof(HandshakeRecord));
}

void test_seal_and_validate_roundtrip() {
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    HandshakeRecord r = makeRecord(bssid, "TestNet");
    handshakeRecordSeal(r);
    TEST_ASSERT_EQUAL_UINT8(RECORD_VERSION, r.recVersion);
    TEST_ASSERT_TRUE(handshakeRecordValid(r));
}

void test_crc_detects_corruption() {
    uint8_t bssid[6] = {1, 2, 3, 4, 5, 6};
    HandshakeRecord r = makeRecord(bssid, "Net");
    handshakeRecordSeal(r);
    TEST_ASSERT_TRUE(handshakeRecordValid(r));

    r.channel ^= 0xFF;  // flip a covered byte without re-sealing
    TEST_ASSERT_FALSE(handshakeRecordValid(r));
}

void test_validate_rejects_bad_state_and_unterminated_ssid() {
    uint8_t bssid[6] = {9, 9, 9, 9, 9, 9};
    HandshakeRecord r = makeRecord(bssid, "X");
    r.state = 200;  // not ACTIVE/TOMBSTONE
    handshakeRecordSeal(r);
    TEST_ASSERT_FALSE(handshakeRecordValid(r));

    HandshakeRecord r2 = makeRecord(bssid, "Y");
    memset(r2.ssid, 'A', sizeof(r2.ssid));  // no NUL terminator
    handshakeRecordSeal(r2);
    TEST_ASSERT_FALSE(handshakeRecordValid(r2));
}

// ---- manifest create / append / read ---------------------------------------

void test_create_empty_manifest() {
    ManifestStore m;
    TEST_ASSERT_TRUE(m.open(kPath));
    TEST_ASSERT_TRUE(m.isReady());
    TEST_ASSERT_EQUAL_UINT32(0, m.count());
}

void test_append_then_read_back() {
    ManifestStore m;
    TEST_ASSERT_TRUE(m.open(kPath));

    uint8_t bssid[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    HandshakeRecord in = makeRecord(bssid, "Coffee", 11, 4096);
    uint32_t idx = 99;
    TEST_ASSERT_TRUE(m.append(in, &idx));
    TEST_ASSERT_EQUAL_UINT32(0, idx);
    TEST_ASSERT_EQUAL_UINT32(1, m.count());

    HandshakeRecord out;
    TEST_ASSERT_TRUE(m.readAt(0, out));
    TEST_ASSERT_TRUE(handshakeRecordValid(out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, out.bssid, 6);
    TEST_ASSERT_EQUAL_STRING("Coffee", out.ssid);
    TEST_ASSERT_EQUAL_UINT8(11, out.channel);
    TEST_ASSERT_EQUAL_UINT32(4096, out.pcapSize);
}

void test_multiple_appends_have_stable_offsets() {
    ManifestStore m;
    TEST_ASSERT_TRUE(m.open(kPath));
    for (uint8_t i = 0; i < 5; ++i) {
        uint8_t bssid[6] = {i, i, i, i, i, i};
        char ssid[16];
        snprintf(ssid, sizeof(ssid), "AP%u", i);
        TEST_ASSERT_TRUE(m.append(makeRecord(bssid, ssid)));
    }
    TEST_ASSERT_EQUAL_UINT32(5, m.count());

    for (uint8_t i = 0; i < 5; ++i) {
        HandshakeRecord out;
        TEST_ASSERT_TRUE(m.readAt(i, out));
        char ssid[16];
        snprintf(ssid, sizeof(ssid), "AP%u", i);
        TEST_ASSERT_EQUAL_STRING(ssid, out.ssid);
        TEST_ASSERT_EQUAL_UINT8(i, out.bssid[0]);
    }
}

void test_read_out_of_range_fails() {
    ManifestStore m;
    TEST_ASSERT_TRUE(m.open(kPath));
    HandshakeRecord out;
    TEST_ASSERT_FALSE(m.readAt(0, out));  // empty

    uint8_t bssid[6] = {1, 1, 1, 1, 1, 1};
    m.append(makeRecord(bssid, "A"));
    TEST_ASSERT_FALSE(m.readAt(1, out));  // index == count
}

// ---- in-place update -------------------------------------------------------

void test_write_at_updates_in_place() {
    ManifestStore m;
    TEST_ASSERT_TRUE(m.open(kPath));

    uint8_t b0[6] = {0xA, 0xA, 0xA, 0xA, 0xA, 0xA};
    uint8_t b1[6] = {0xB, 0xB, 0xB, 0xB, 0xB, 0xB};
    m.append(makeRecord(b0, "First"));
    m.append(makeRecord(b1, "Second"));

    // Mutate record 0's WPA-SEC state (the hot path).
    HandshakeRecord r0;
    TEST_ASSERT_TRUE(m.readAt(0, r0));
    r0.wpaSecStatus = 2;  // CRACKED
    r0.flags |= RF_HAS_PASSWORD;
    strncpy(r0.wpaSecPassword, "hunter2hunter2", sizeof(r0.wpaSecPassword) - 1);
    TEST_ASSERT_TRUE(m.writeAt(0, r0));

    HandshakeRecord check0, check1;
    TEST_ASSERT_TRUE(m.readAt(0, check0));
    TEST_ASSERT_TRUE(m.readAt(1, check1));

    TEST_ASSERT_TRUE(handshakeRecordValid(check0));
    TEST_ASSERT_EQUAL_UINT8(2, check0.wpaSecStatus);
    TEST_ASSERT_EQUAL_STRING("hunter2hunter2", check0.wpaSecPassword);
    // Record 1 must be untouched.
    TEST_ASSERT_TRUE(handshakeRecordValid(check1));
    TEST_ASSERT_EQUAL_STRING("Second", check1.ssid);
    TEST_ASSERT_EQUAL_UINT8(0, check1.wpaSecStatus);
}

void test_write_at_out_of_range_fails() {
    ManifestStore m;
    TEST_ASSERT_TRUE(m.open(kPath));
    uint8_t b[6] = {7, 7, 7, 7, 7, 7};
    HandshakeRecord r = makeRecord(b, "Z");
    TEST_ASSERT_FALSE(m.writeAt(0, r));  // empty manifest
}

// ---- persistence -----------------------------------------------------------

void test_persists_across_reopen() {
    {
        ManifestStore m;
        TEST_ASSERT_TRUE(m.open(kPath));
        uint8_t b[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
        m.append(makeRecord(b, "Persisted", 3, 7777));
    }
    ManifestStore m2;
    TEST_ASSERT_TRUE(m2.open(kPath));
    TEST_ASSERT_EQUAL_UINT32(1, m2.count());
    HandshakeRecord out;
    TEST_ASSERT_TRUE(m2.readAt(0, out));
    TEST_ASSERT_EQUAL_STRING("Persisted", out.ssid);
    TEST_ASSERT_EQUAL_UINT32(7777, out.pcapSize);
}

// ---- crash-safety / torn append reconciliation -----------------------------

void test_torn_trailing_append_is_ignored() {
    {
        ManifestStore m;
        TEST_ASSERT_TRUE(m.open(kPath));
        uint8_t b0[6] = {1, 0, 0, 0, 0, 0};
        uint8_t b1[6] = {2, 0, 0, 0, 0, 0};
        m.append(makeRecord(b0, "One"));
        m.append(makeRecord(b1, "Two"));
        TEST_ASSERT_EQUAL_UINT32(2, m.count());
    }
    // Simulate a crash mid-append: extra partial record bytes, count not bumped.
    FILE* f = fopen(kPath, "r+");
    TEST_ASSERT_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    uint8_t garbage[100];
    memset(garbage, 0xAB, sizeof(garbage));
    fwrite(garbage, 1, sizeof(garbage), f);
    fclose(f);

    ManifestStore m2;
    TEST_ASSERT_TRUE(m2.open(kPath));
    TEST_ASSERT_EQUAL_UINT32(2, m2.count());  // partial trailing record dropped
    HandshakeRecord out;
    TEST_ASSERT_TRUE(m2.readAt(1, out));
    TEST_ASSERT_EQUAL_STRING("Two", out.ssid);
}

void test_header_count_clamped_to_file_size() {
    {
        ManifestStore m;
        TEST_ASSERT_TRUE(m.open(kPath));
        uint8_t b[6] = {5, 0, 0, 0, 0, 0};
        m.append(makeRecord(b, "Solo"));
    }
    // Corrupt the header count to claim more records than the file holds.
    FILE* f = fopen(kPath, "r+");
    TEST_ASSERT_NOT_NULL(f);
    fseek(f, ManifestStore::COUNT_OFFSET, SEEK_SET);
    uint32_t bogus = 9;
    fwrite(&bogus, sizeof(bogus), 1, f);
    fclose(f);

    ManifestStore m2;
    TEST_ASSERT_TRUE(m2.open(kPath));
    TEST_ASSERT_EQUAL_UINT32(1, m2.count());  // clamped to actual file size
}

void test_new_manifest_is_not_authoritative() {
    ManifestStore m;
    TEST_ASSERT_TRUE(m.open(kPath));
    TEST_ASSERT_FALSE(m.isAuthoritative());  // freshly created -> dir not reconciled
}

void test_mark_authoritative_persists_across_reopen() {
    {
        ManifestStore m;
        TEST_ASSERT_TRUE(m.open(kPath));
        uint8_t b[6] = {1, 2, 3, 4, 5, 6};
        m.append(makeRecord(b, "One"));
        TEST_ASSERT_FALSE(m.isAuthoritative());
        TEST_ASSERT_TRUE(m.markAuthoritative());
        TEST_ASSERT_TRUE(m.isAuthoritative());
    }
    // Flag survives reopen and doesn't disturb the record count.
    ManifestStore m2;
    TEST_ASSERT_TRUE(m2.open(kPath));
    TEST_ASSERT_TRUE(m2.isAuthoritative());
    TEST_ASSERT_EQUAL_UINT32(1, m2.count());

    // Appending after the flag is set keeps it set (and readable record intact).
    uint8_t b2[6] = {7, 7, 7, 7, 7, 7};
    TEST_ASSERT_TRUE(m2.append(makeRecord(b2, "Two")));
    TEST_ASSERT_TRUE(m2.isAuthoritative());
    HandshakeRecord out;
    TEST_ASSERT_TRUE(m2.readAt(0, out));
    TEST_ASSERT_EQUAL_STRING("One", out.ssid);
}

void test_compact_drops_tombstones_and_preserves_order_and_flags() {
    {
        ManifestStore m;
        TEST_ASSERT_TRUE(m.open(kPath));
        uint8_t b0[6] = {0, 0, 0, 0, 0, 1};
        uint8_t b1[6] = {0, 0, 0, 0, 0, 2};
        uint8_t b2[6] = {0, 0, 0, 0, 0, 3};
        m.append(makeRecord(b0, "Alpha"));
        m.append(makeRecord(b1, "Bravo"));
        m.append(makeRecord(b2, "Charlie"));
        TEST_ASSERT_TRUE(m.markAuthoritative());

        // Tombstone the middle record in place.
        HandshakeRecord mid;
        TEST_ASSERT_TRUE(m.readAt(1, mid));
        mid.state = static_cast<uint8_t>(RecordState::TOMBSTONE);
        TEST_ASSERT_TRUE(m.writeAt(1, mid));

        TEST_ASSERT_TRUE(m.compact());
        TEST_ASSERT_EQUAL_UINT32(2, m.count());        // Bravo dropped
        TEST_ASSERT_TRUE(m.isAuthoritative());         // flag preserved

        HandshakeRecord r0, r1;
        TEST_ASSERT_TRUE(m.readAt(0, r0));
        TEST_ASSERT_TRUE(m.readAt(1, r1));
        TEST_ASSERT_EQUAL_STRING("Alpha", r0.ssid);    // survivors keep order
        TEST_ASSERT_EQUAL_STRING("Charlie", r1.ssid);
        TEST_ASSERT_TRUE(handshakeRecordValid(r0));
        TEST_ASSERT_TRUE(handshakeRecordValid(r1));
    }
    // Compacted result survives a reopen and the temp file is gone.
    ManifestStore m2;
    TEST_ASSERT_TRUE(m2.open(kPath));
    TEST_ASSERT_EQUAL_UINT32(2, m2.count());
    TEST_ASSERT_TRUE(m2.isAuthoritative());
}

void test_open_recovers_interrupted_compaction_temp() {
    // Simulate a crash mid-swap: the original is gone, only the .tmp survives.
    {
        ManifestStore m;
        TEST_ASSERT_TRUE(m.open(kPath));
        uint8_t b[6] = {9, 9, 9, 9, 9, 9};
        m.append(makeRecord(b, "Survivor"));
    }
    TEST_ASSERT_EQUAL_INT(0, ::rename(kPath, kTmpPath));  // bin -> tmp, bin missing

    ManifestStore m2;
    TEST_ASSERT_TRUE(m2.open(kPath));                     // promotes the temp
    TEST_ASSERT_EQUAL_UINT32(1, m2.count());
    HandshakeRecord out;
    TEST_ASSERT_TRUE(m2.readAt(0, out));
    TEST_ASSERT_EQUAL_STRING("Survivor", out.ssid);
}

void test_open_rejects_bad_magic() {
    FILE* f = fopen(kPath, "w");
    TEST_ASSERT_NOT_NULL(f);
    uint8_t junk[64];
    memset(junk, 0x55, sizeof(junk));
    fwrite(junk, 1, sizeof(junk), f);
    fclose(f);

    ManifestStore m;
    TEST_ASSERT_FALSE(m.open(kPath));  // bad header -> caller rebuilds
    TEST_ASSERT_FALSE(m.isReady());
}

// ---- runner ----------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_record_is_256_bytes);
    RUN_TEST(test_seal_and_validate_roundtrip);
    RUN_TEST(test_crc_detects_corruption);
    RUN_TEST(test_validate_rejects_bad_state_and_unterminated_ssid);
    RUN_TEST(test_create_empty_manifest);
    RUN_TEST(test_append_then_read_back);
    RUN_TEST(test_multiple_appends_have_stable_offsets);
    RUN_TEST(test_read_out_of_range_fails);
    RUN_TEST(test_write_at_updates_in_place);
    RUN_TEST(test_write_at_out_of_range_fails);
    RUN_TEST(test_persists_across_reopen);
    RUN_TEST(test_torn_trailing_append_is_ignored);
    RUN_TEST(test_header_count_clamped_to_file_size);
    RUN_TEST(test_new_manifest_is_not_authoritative);
    RUN_TEST(test_mark_authoritative_persists_across_reopen);
    RUN_TEST(test_compact_drops_tombstones_and_preserves_order_and_flags);
    RUN_TEST(test_open_recovers_interrupted_compaction_temp);
    RUN_TEST(test_open_rejects_bad_magic);
    return UNITY_END();
}
