#include <gtest/gtest.h>

#include <freerdp/codec/color.h>
#include <freerdp/codec/h264.h>
#include <algorithm>
#include <array>
#include <memory>
#include <span>

namespace {

// Project-generated constant-red 64x64 AVC420 fixture, SPS/PPS + one IDR.
// ffmpeg -f lavfi -i color=c=red:s=64x64:r=1 -frames:v 1 -c:v libx264
//   -profile:v baseline -preset ultrafast -tune zerolatency
//   -vf scale=in_range=tv:out_range=pc:in_color_matrix=bt601:out_color_matrix=bt709
//   -color_range pc -colorspace bt709
//   -x264-params keyint=1:repeat-headers=1 -f h264 fixture.h264
// The informational SEI NAL was omitted; this is test data, not third-party code.
constexpr std::array<BYTE, 71> kRedFrame{0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xdc, 0x42, 0x6c, 0x05, 0xb8, 0x10, 0x10, 0x0a, 0x00, 0x00,
                                         0x03, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x05, 0x1e, 0x24, 0x4f, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x0f,
                                         0xc8, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0x11, 0x8a, 0x00, 0x02, 0x29, 0x71, 0xc0, 0x00, 0x40, 0x9e,
                                         0x38, 0x00, 0x08, 0x8a, 0x49, 0xc9, 0xc9, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xe0};

struct DecoderCloser final {
    void operator()(H264_CONTEXT* context) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): owned FreeRDP deleter ABI.
        h264_context_free(context);
    }
};
using Decoder = std::unique_ptr<H264_CONTEXT, DecoderCloser>;
using Surface = std::array<BYTE, 64 * 64 * 4>;

int Decode(H264_CONTEXT& decoder, std::span<const BYTE> data, Surface& surface) {
    const RECTANGLE_16 rectangle{0, 0, 64, 64};
    return avc420_decompress(&decoder, data.data(), static_cast<UINT32>(data.size()), surface.data(), PIXEL_FORMAT_BGRA32, 64 * 4, 64, 64, &rectangle,
                             1);
}

void ExpectRed(const Surface& surface) {
    for (std::size_t pixel{0}; pixel < 64 * 64; ++pixel) {
        ASSERT_LT(surface[pixel * 4], 15);
        ASSERT_LT(surface[pixel * 4 + 1], 15);
        ASSERT_GT(surface[pixel * 4 + 2], 235);
    }
}

TEST(RdpDecoder, FirstOutputIsDrainedAfterFormatChangeAndRepeatedReset) {
    const Decoder decoder{h264_context_new(FALSE)};
    ASSERT_TRUE(decoder);
    for (int iteration{0}; iteration < 8; ++iteration) {
        ASSERT_TRUE(h264_context_reset(decoder.get(), 64, 64));
        Surface surface{};
        surface.fill(0x5a);
        ASSERT_GE(Decode(*decoder, kRedFrame, surface), 0);
        ExpectRed(surface);
    }
}

TEST(RdpDecoder, HeadersWithoutPictureDoNotReadAnEmptySurface) {
    for (int iteration{0}; iteration < 8; ++iteration) {
        const Decoder decoder{h264_context_new(FALSE)};
        ASSERT_TRUE(decoder);
        ASSERT_TRUE(h264_context_reset(decoder.get(), 64, 64));
        Surface surface{};
        surface.fill(0x5a);
        ASSERT_GE(Decode(*decoder, std::span{kRedFrame}.first(37), surface), 0);
        EXPECT_TRUE(std::ranges::all_of(surface, [](BYTE value) { return value == 0x5a; }));
        ASSERT_GE(Decode(*decoder, kRedFrame, surface), 0);
        ExpectRed(surface);
    }
}

} // namespace
