#include "px_render/architecture/encoders/nvenc/annex_b_keyframe.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace px {
namespace {

TEST(AnnexBKeyFrame, DetectsH264IdrAfterParameterSets) {
    const std::vector<std::uint8_t> idr{0, 0, 0, 1, 0x67, 1, 2, 0, 0, 1, 0x68, 3, 0, 0, 1, 0x65, 4, 5};
    ASSERT_TRUE(DetectAnnexBRandomAccess(idr, AnnexBVideoCodec::kH264).has_value());
    EXPECT_TRUE(*DetectAnnexBRandomAccess(idr, AnnexBVideoCodec::kH264));
}

TEST(AnnexBKeyFrame, RejectsH264PredictedSlice) {
    const std::vector<std::uint8_t> predicted{0, 0, 1, 0x41, 1, 2, 3};
    ASSERT_TRUE(DetectAnnexBRandomAccess(predicted, AnnexBVideoCodec::kH264).has_value());
    EXPECT_FALSE(*DetectAnnexBRandomAccess(predicted, AnnexBVideoCodec::kH264));
}

TEST(AnnexBKeyFrame, DetectsH265CraAndIdr) {
    for (const std::uint8_t nal_type : {std::uint8_t{19}, std::uint8_t{20}, std::uint8_t{21}}) {
        const std::vector<std::uint8_t> random_access{0, 0, 0, 1, static_cast<std::uint8_t>(nal_type << 1U), 1, 2};
        ASSERT_TRUE(DetectAnnexBRandomAccess(random_access, AnnexBVideoCodec::kH265).has_value());
        EXPECT_TRUE(*DetectAnnexBRandomAccess(random_access, AnnexBVideoCodec::kH265));
    }
}

TEST(AnnexBKeyFrame, ReturnsUnknownWithoutVclNal) {
    const std::vector<std::uint8_t> parameter_sets{0, 0, 0, 1, 0x67, 1, 2, 3};
    EXPECT_FALSE(DetectAnnexBRandomAccess(parameter_sets, AnnexBVideoCodec::kH264).has_value());
}

} // namespace
} // namespace px
