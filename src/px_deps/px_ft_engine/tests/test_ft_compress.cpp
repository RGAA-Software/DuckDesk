// 块压缩单测 - 对照 rustdesk/libs/hbb_common/src/compress.rs 的测试与 fs.rs:454
#include <gtest/gtest.h>

#include <array>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "ft_compress.h"

namespace px::ft {
namespace {

std::vector<uint8_t> MakeRandom(size_t n) {
    std::mt19937 rng(42);
    std::vector<uint8_t> v(n);
    for (auto& b : v) b = static_cast<uint8_t>(rng());
    return v;
}

// fs.rs:454 is_compressed_file
TEST(FtCompress, CompressedFileExts) {
    constexpr std::array<std::string_view, 9> compressed_names{
        "a.xz", "a.gz", "a.zip", "a.7z", "a.rar", "a.bz2", "a.tgz", "a.png", "a.jpg"};
    for (const auto name : compressed_names) {
        EXPECT_TRUE(IsCompressedFile(name)) << name;
    }
    constexpr std::array<std::string_view, 5> plain_names{
        "a.txt", "a.bin", "a", "a.ZIP", "a.zipp"};
    for (const auto name : plain_names) {
        EXPECT_FALSE(IsCompressedFile(name)) << name; // 大小写敏感,与上游一致
    }
    // 注意:".zip" 这类纯扩展名文件名,上游 get_ext 同样判为压缩文件(fs.rs:447)
}

// 文本压缩往返 + 压缩率
TEST(FtCompress, TextRoundtripCompresses) {
    std::string text;
    for (int i = 0; i < 5000; ++i) text += "the quick brown fox jumps over the lazy dog\n";
    std::vector<uint8_t> data(text.begin(), text.end());
    auto compressed = Compress(data);
    ASSERT_FALSE(compressed.empty());
    EXPECT_LT(compressed.size(), data.size() / 4); // 高重复文本应有显著压缩率
    auto decompressed = Decompress(compressed);
    EXPECT_EQ(decompressed, data);
}

// 随机数据往返(压缩不生效时引擎走未压缩路径,但 Decompress(Compress(x)) 必须还原)
TEST(FtCompress, RandomRoundtrip) {
    auto data = MakeRandom(200 * 1024);
    auto compressed = Compress(data);
    ASSERT_FALSE(compressed.empty());
    auto decompressed = Decompress(compressed);
    EXPECT_EQ(decompressed, data);
}

// 空输入
TEST(FtCompress, EmptyInput) {
    EXPECT_TRUE(Compress(std::span<const uint8_t>{}).empty());
    EXPECT_TRUE(Decompress(std::span<const uint8_t>{}).empty());
}

// 损坏数据 -> 空(对应 compress.rs 错误路径返回默认值)
TEST(FtCompress, CorruptedDataRejected) {
    auto garbage = MakeRandom(1024);
    EXPECT_TRUE(Decompress(garbage).empty());
}

// compress.rs:61 rejects_data_larger_than_limit
TEST(FtCompress, RejectsDataLargerThanLimit) {
    std::vector<uint8_t> zeros(1025, 0);
    auto compressed = Compress(zeros);
    ASSERT_FALSE(compressed.empty());
    EXPECT_TRUE(DecompressWithLimit(compressed, 1024).empty());
}

// compress.rs:67 accepts_data_at_limit
TEST(FtCompress, AcceptsDataAtLimit) {
    std::vector<uint8_t> zeros(1024, 0);
    auto compressed = Compress(zeros);
    ASSERT_FALSE(compressed.empty());
    auto out = DecompressWithLimit(compressed, 1024);
    EXPECT_EQ(out, zeros);
}

} // namespace
} // namespace px::ft
