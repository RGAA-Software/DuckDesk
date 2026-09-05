#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "../path_codec.h"

namespace px {
namespace {

TEST(PathCodecTest, RoundTripsPortableUtf8Paths) {
    const std::vector<std::string> paths{
        "",
        "plain/path.txt",
        "space dir/file name.txt",
        "GammaRay/中文/文件.txt",
        "GammaRay/日本語/映像.dat",
        "GammaRay/emoji-\xF0\x9F\x9A\x80/file.bin",
        "GammaRay/e\xCC\x81/combined.txt",
    };

    for (const auto& value : paths) {
        const auto native = PathFromUtf8(value);
        ASSERT_TRUE(native) << value;
        const auto round_trip = PathToUtf8(native.Value());
        ASSERT_TRUE(round_trip) << value;
        EXPECT_EQ(round_trip.Value(), value);
    }
}

TEST(PathCodecTest, RejectsInvalidUtf8) {
    const std::vector<std::string> invalid{
        std::string{"\x80", 1},
        std::string{"\xC0\xAF", 2},
        std::string{"\xE4\xB8", 2},
        std::string{"\xED\xA0\x80", 3},
        std::string{"\xF4\x90\x80\x80", 4},
    };

    for (const auto& value : invalid) {
        EXPECT_FALSE(IsValidUtf8(value));
        const auto result = PathFromUtf8(value);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.Error().detail_code, "PATH_INVALID_UTF8");
    }
}

#if defined(_WIN32)
TEST(PathCodecTest, RejectsInvalidNativeUtf16) {
    const std::filesystem::path invalid(std::wstring(1, static_cast<wchar_t>(0xd800)));
    const auto result = PathToUtf8(invalid);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().detail_code, "PATH_NATIVE_TO_UTF8_FAILED");
}
#endif

} // namespace
} // namespace px
