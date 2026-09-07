#include "sdk_stream_helper.h"

#include <gtest/gtest.h>

#include <string>

namespace px {
namespace {

std::string StartCode4() {
    return std::string{'\0', '\0', '\0', '\1'};
}

std::string StartCode3() {
    return std::string{'\0', '\0', '\1'};
}

TEST(SdkStreamHelperTest, ExtractsH264ParameterSetsAcrossThreeAndFourByteStartCodes) {
    const auto sps = StartCode4() + std::string{'\x67', '\x64', '\0', '\x28'};
    const auto pps = StartCode3() + std::string{'\x68', '\x01', '\x02'};
    const auto frame = std::string{'x', 'x'} + sps + StartCode4() + std::string{'\x65', '\x55'} + pps;

    const auto result = StreamHelper::ExtractH264ParameterSets(frame);

    EXPECT_EQ(result.sps, sps);
    EXPECT_EQ(result.pps, pps);
}

TEST(SdkStreamHelperTest, ExtractsOnlyH265VpsSpsAndPps) {
    const auto vps = StartCode4() + std::string{'\x40', '\x01'};
    const auto sps = StartCode3() + std::string{'\x42', '\x01'};
    const auto picture = StartCode4() + std::string{'\x26', '\x01', '\x55'};
    const auto pps = StartCode3() + std::string{'\x44', '\x01'};

    EXPECT_EQ(StreamHelper::ExtractH265ParameterSets(vps + sps + picture + pps), vps + sps + pps);
}

TEST(SdkStreamHelperTest, RejectsTruncatedOrNonAnnexBInput) {
    EXPECT_TRUE(StreamHelper::ExtractH264ParameterSets(std::string{'\0', '\0', '\0', '\1'}).sps.empty());
    EXPECT_TRUE(StreamHelper::ExtractH265ParameterSets("length-prefixed").empty());
}

} // namespace
} // namespace px
