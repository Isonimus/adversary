/**
 * @file test_storage_overview.cpp
 * @brief Native regression for `computeStorageRow()` (ADR-0166, slice-0167).
 *
 * The SD-backed `IDirectoryLister` crosses the HAL boundary and is verified on
 * device; this covers the pure walk logic it feeds into: empty dir, files-only
 * counting with subdir skipping, the STORAGE_WALK_CAP truncation, and the
 * missing-dir failure mode.
 */

#include <unity.h>
#include <cstring>
#include <vector>

#include "utils/storage_overview.h"

using adversary::DirEntry;
using adversary::IDirectoryLister;
using adversary::StorageRow;
using adversary::STORAGE_WALK_CAP;
using adversary::computeStorageRow;

namespace {

struct StubEntry {
    const char* name;
    uint32_t size;
    bool isDirectory;
};

class StubLister : public IDirectoryLister {
public:
    StubLister(const char* expectedPath, std::vector<StubEntry> entries,
               bool openOk = true)
        : expectedPath_(expectedPath), entries_(std::move(entries)), openOk_(openOk) {}

    bool open(const char* path) override {
        if (strcmp(path, expectedPath_) != 0) return false;
        opened_ = openOk_;
        return opened_;
    }

    bool next(DirEntry& out) override {
        if (!opened_) return false;
        if (index_ >= entries_.size()) return false;
        const auto& e = entries_[index_++];
        out.name = e.name;
        out.size = e.size;
        out.isDirectory = e.isDirectory;
        return true;
    }

    void close() override {
        opened_ = false;
        index_ = 0;
    }

private:
    const char* expectedPath_;
    std::vector<StubEntry> entries_;
    bool openOk_;
    bool opened_ = false;
    size_t index_ = 0;
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_empty_dir_returns_ok_with_zero_counts(void) {
    StubLister lister("/adversary/captures/handshakes", {});
    StorageRow row = computeStorageRow(lister, "/adversary/captures/handshakes");
    TEST_ASSERT_TRUE(row.ok);
    TEST_ASSERT_EQUAL_UINT16(0, row.fileCount);
    TEST_ASSERT_EQUAL_UINT64(0u, row.totalBytes);
    TEST_ASSERT_FALSE(row.truncated);
}

void test_files_counted_subdirs_skipped(void) {
    StubLister lister("/adversary/ir", {
        {"code1.ir", 200, false},
        {"nested",     0, true},   // subdir — ignored
        {"code2.ir", 300, false},
        {"other",      0, true},   // subdir — ignored
        {"code3.ir", 500, false},
    });
    StorageRow row = computeStorageRow(lister, "/adversary/ir");
    TEST_ASSERT_TRUE(row.ok);
    TEST_ASSERT_EQUAL_UINT16(3, row.fileCount);
    TEST_ASSERT_EQUAL_UINT64(1000u, row.totalBytes);
    TEST_ASSERT_FALSE(row.truncated);
}

void test_truncates_above_cap(void) {
    std::vector<StubEntry> entries;
    entries.reserve(STORAGE_WALK_CAP + 100);
    for (uint16_t i = 0; i < STORAGE_WALK_CAP + 100; ++i) {
        entries.push_back({"f", 10, false});
    }
    StubLister lister("/x", std::move(entries));
    StorageRow row = computeStorageRow(lister, "/x");
    TEST_ASSERT_TRUE(row.ok);
    TEST_ASSERT_TRUE(row.truncated);
    TEST_ASSERT_EQUAL_UINT16(STORAGE_WALK_CAP, row.fileCount);
    TEST_ASSERT_EQUAL_UINT64(
        static_cast<uint64_t>(STORAGE_WALK_CAP) * 10u, row.totalBytes);
}

void test_missing_dir_returns_not_ok(void) {
    StubLister lister("/missing", {}, /*openOk=*/false);
    StorageRow row = computeStorageRow(lister, "/missing");
    TEST_ASSERT_FALSE(row.ok);
    TEST_ASSERT_EQUAL_UINT16(0, row.fileCount);
    TEST_ASSERT_EQUAL_UINT64(0u, row.totalBytes);
    TEST_ASSERT_FALSE(row.truncated);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_dir_returns_ok_with_zero_counts);
    RUN_TEST(test_files_counted_subdirs_skipped);
    RUN_TEST(test_truncates_above_cap);
    RUN_TEST(test_missing_dir_returns_not_ok);
    return UNITY_END();
}
