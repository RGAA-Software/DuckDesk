//
// Unit tests for records_catalog (design doc section 9.1)
//

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "render_panel/network/records_catalog.h"

namespace fs = std::filesystem;

namespace {

    class RecordsCatalogTest : public testing::Test {
    protected:
        void SetUp() override {
            dir_ = fs::temp_directory_path() / fs::path("test_records_catalog_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(dir_);
        }
        void TearDown() override {
            std::error_code ec;
            fs::remove_all(dir_, ec);
        }
        void Touch(const std::string& name, const std::string& content = "x") {
            std::ofstream ofs(dir_ / name, std::ios::binary | std::ios::trunc);
            ofs << content;
        }
        fs::path dir_;
    };

}

// ---- file name whitelist ----

TEST(RecordFileNameValidation, ValidNames) {
    EXPECT_TRUE(px::IsValidRecordFileName("rec_DISPLAY1_20260817_10.30.00.mp4"));
    EXPECT_TRUE(px::IsValidRecordFileName("rec_mon_with_underscore_20260817_10.30.00_1.mp4"));
    EXPECT_TRUE(px::IsValidRecordFileName("a-b_c.D.mp4"));
}

TEST(RecordFileNameValidation, RejectsNonMp4) {
    EXPECT_FALSE(px::IsValidRecordFileName("rec_DISPLAY1_20260817_10.30.00.mkv"));
    EXPECT_FALSE(px::IsValidRecordFileName("rec_DISPLAY1_20260817_10.30.00.mp4.recording"));
    EXPECT_FALSE(px::IsValidRecordFileName("rec_DISPLAY1_20260817_10.30.00"));
}

TEST(RecordFileNameValidation, RejectsTraversalAndIllegalChars) {
    EXPECT_FALSE(px::IsValidRecordFileName("../etc/passwd.mp4"));
    EXPECT_FALSE(px::IsValidRecordFileName("..mp4.mp4"));
    EXPECT_FALSE(px::IsValidRecordFileName("sub/dir.mp4"));
    EXPECT_FALSE(px::IsValidRecordFileName("sub\\dir.mp4"));
    EXPECT_FALSE(px::IsValidRecordFileName("a b.mp4"));
    EXPECT_FALSE(px::IsValidRecordFileName("中文.mp4"));
    EXPECT_FALSE(px::IsValidRecordFileName(".mp4"));
    EXPECT_FALSE(px::IsValidRecordFileName(""));
}

// ---- record file name parsing ----

TEST(RecordFileNameParse, SimpleMonitor) {
    std::string monitor;
    int64_t ts = 0;
    ASSERT_TRUE(px::ParseRecordFileName("rec_DISPLAY1_20260817_10.30.05.mp4", monitor, ts));
    EXPECT_EQ(monitor, "DISPLAY1");
    EXPECT_GT(ts, 0);
}

TEST(RecordFileNameParse, MonitorWithUnderscore) {
    std::string monitor;
    int64_t ts = 0;
    ASSERT_TRUE(px::ParseRecordFileName("rec_mi_monitor_2_20260817_10.30.05.mp4", monitor, ts));
    EXPECT_EQ(monitor, "mi_monitor_2");
}

TEST(RecordFileNameParse, CollisionSuffix) {
    std::string monitor;
    int64_t ts = 0;
    ASSERT_TRUE(px::ParseRecordFileName("rec_DISPLAY1_20260817_10.30.05_1.mp4", monitor, ts));
    EXPECT_EQ(monitor, "DISPLAY1");

    ASSERT_TRUE(px::ParseRecordFileName("rec_mi_monitor_20260817_10.30.05_12.mp4", monitor, ts));
    EXPECT_EQ(monitor, "mi_monitor");
}

TEST(RecordFileNameParse, TimestampValue) {
    // rec_X_19700102_08.00.01 -> local time; just verify consistency with mktime
    std::string monitor;
    int64_t ts = 0;
    ASSERT_TRUE(px::ParseRecordFileName("rec_X_20260817_10.30.05.mp4", monitor, ts));
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 8 - 1;
    tm.tm_mday = 17;
    tm.tm_hour = 10;
    tm.tm_min = 30;
    tm.tm_sec = 5;
    tm.tm_isdst = -1;
    EXPECT_EQ(ts, static_cast<int64_t>(std::mktime(&tm)));
}

TEST(RecordFileNameParse, RejectsBadNames) {
    std::string monitor;
    int64_t ts = 0;
    EXPECT_FALSE(px::ParseRecordFileName("foo.mp4", monitor, ts));
    EXPECT_FALSE(px::ParseRecordFileName("rec_DISPLAY1.mp4", monitor, ts));
    EXPECT_FALSE(px::ParseRecordFileName("rec__20260817_10.30.05.mp4", monitor, ts));      // empty monitor
    EXPECT_FALSE(px::ParseRecordFileName("rec_DISPLAY1_2026081_10.30.05.mp4", monitor, ts)); // 7-digit date
    EXPECT_FALSE(px::ParseRecordFileName("rec_DISPLAY1_20260817_10-30-05.mp4", monitor, ts));
}

// ---- sidecar + scan ----

TEST_F(RecordsCatalogTest, ScanFiltersRecordingSidecar) {
    Touch("rec_DISPLAY1_20260817_10.30.00.mp4", "finished");
    Touch("rec_DISPLAY1_20260817_10.31.00.mp4", "recording");
    Touch("rec_DISPLAY1_20260817_10.31.00.mp4.recording");
    Touch("unrelated.txt");

    auto files = px::ScanRecordFiles(dir_);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].name, "rec_DISPLAY1_20260817_10.30.00.mp4");
    EXPECT_EQ(files[0].monitor, "DISPLAY1");
    EXPECT_EQ(files[0].size, 8u);
    EXPECT_EQ(files[0].codec, "h264");
}

TEST_F(RecordsCatalogTest, HasRecordingSidecar) {
    Touch("a.mp4");
    EXPECT_FALSE(px::HasRecordingSidecar(dir_ / "a.mp4"));
    Touch("b.mp4");
    Touch("b.mp4.recording");
    EXPECT_TRUE(px::HasRecordingSidecar(dir_ / "b.mp4"));
}

TEST_F(RecordsCatalogTest, ScanNonExistDir) {
    auto files = px::ScanRecordFiles(dir_ / "no_such_dir");
    EXPECT_TRUE(files.empty());
}

// ---- Range parsing ----

TEST(RangeParse, NoHeader) {
    px::ByteRange r;
    EXPECT_EQ(px::ParseRangeHeader("", 1000, r), px::RangeParseResult::kNone);
}

TEST(RangeParse, FullRange) {
    px::ByteRange r;
    ASSERT_EQ(px::ParseRangeHeader("bytes=0-99", 1000, r), px::RangeParseResult::kOk);
    EXPECT_EQ(r.begin, 0u);
    EXPECT_EQ(r.end, 99u);
}

TEST(RangeParse, OpenEnded) {
    px::ByteRange r;
    ASSERT_EQ(px::ParseRangeHeader("bytes=100-", 1000, r), px::RangeParseResult::kOk);
    EXPECT_EQ(r.begin, 100u);
    EXPECT_EQ(r.end, 999u);
}

TEST(RangeParse, Suffix) {
    px::ByteRange r;
    ASSERT_EQ(px::ParseRangeHeader("bytes=-100", 1000, r), px::RangeParseResult::kOk);
    EXPECT_EQ(r.begin, 900u);
    EXPECT_EQ(r.end, 999u);
}

TEST(RangeParse, SuffixLargerThanFile) {
    px::ByteRange r;
    ASSERT_EQ(px::ParseRangeHeader("bytes=-5000", 1000, r), px::RangeParseResult::kOk);
    EXPECT_EQ(r.begin, 0u);
    EXPECT_EQ(r.end, 999u);
}

TEST(RangeParse, EndClampedToFileSize) {
    px::ByteRange r;
    ASSERT_EQ(px::ParseRangeHeader("bytes=0-9999", 1000, r), px::RangeParseResult::kOk);
    EXPECT_EQ(r.end, 999u);
}

TEST(RangeParse, BeginOutOfBounds) {
    px::ByteRange r;
    EXPECT_EQ(px::ParseRangeHeader("bytes=1000-", 1000, r), px::RangeParseResult::kUnsatisfiable);
    EXPECT_EQ(px::ParseRangeHeader("bytes=1000-2000", 1000, r), px::RangeParseResult::kUnsatisfiable);
}

TEST(RangeParse, EndBeforeBegin) {
    px::ByteRange r;
    EXPECT_EQ(px::ParseRangeHeader("bytes=500-400", 1000, r), px::RangeParseResult::kUnsatisfiable);
}

TEST(RangeParse, ZeroSuffix) {
    px::ByteRange r;
    EXPECT_EQ(px::ParseRangeHeader("bytes=-0", 1000, r), px::RangeParseResult::kUnsatisfiable);
}

TEST(RangeParse, MultiRangeRejected) {
    px::ByteRange r;
    EXPECT_EQ(px::ParseRangeHeader("bytes=0-99,200-299", 1000, r), px::RangeParseResult::kInvalid);
}

TEST(RangeParse, Malformed) {
    px::ByteRange r;
    EXPECT_EQ(px::ParseRangeHeader("bytes=", 1000, r), px::RangeParseResult::kInvalid);
    EXPECT_EQ(px::ParseRangeHeader("bytes=-", 1000, r), px::RangeParseResult::kInvalid);
    EXPECT_EQ(px::ParseRangeHeader("bytes=a-b", 1000, r), px::RangeParseResult::kInvalid);
    EXPECT_EQ(px::ParseRangeHeader("bytes=1-2-3", 1000, r), px::RangeParseResult::kInvalid);
    EXPECT_EQ(px::ParseRangeHeader("items=0-99", 1000, r), px::RangeParseResult::kInvalid);
    EXPECT_EQ(px::ParseRangeHeader("bytes=99999999999999999999999-", 1000, r), px::RangeParseResult::kInvalid);
}

TEST(RangeParse, SliceClampedTo64MB) {
    px::ByteRange r;
    const uint64_t big = 200ull * 1024 * 1024;
    ASSERT_EQ(px::ParseRangeHeader("bytes=0-", big, r), px::RangeParseResult::kOk);
    EXPECT_EQ(r.begin, 0u);
    EXPECT_EQ(r.end, px::kMaxRangeSliceBytes - 1);
}

TEST(RangeParse, SliceClampWithOffset) {
    px::ByteRange r;
    const uint64_t big = 200ull * 1024 * 1024;
    const uint64_t begin = 1000;
    ASSERT_EQ(px::ParseRangeHeader("bytes=1000-", big, r), px::RangeParseResult::kOk);
    EXPECT_EQ(r.end, begin + px::kMaxRangeSliceBytes - 1);
}

TEST(RangeParse, EmptyFile) {
    px::ByteRange r;
    EXPECT_EQ(px::ParseRangeHeader("bytes=0-", 0, r), px::RangeParseResult::kUnsatisfiable);
}
