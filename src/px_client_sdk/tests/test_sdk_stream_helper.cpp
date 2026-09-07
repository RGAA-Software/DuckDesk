#include "sdk_stream_helper.h"
#include "decoder_startup_gate.h"

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

TEST(SdkStreamHelperTest, StartupConfigurationRequiresEveryParameterSet) {
    const auto sps = StartCode4() + std::string{'\x67', '\x64'};
    const auto pps = StartCode3() + std::string{'\x68', '\x01'};
    EXPECT_FALSE(StreamHelper::HasDecoderConfiguration(false, sps));
    EXPECT_FALSE(StreamHelper::HasDecoderConfiguration(false, pps));
    EXPECT_TRUE(StreamHelper::HasDecoderConfiguration(false, sps + pps));
    const auto vps265 = StartCode4() + std::string{'\x40', '\x01'};
    const auto sps265 = StartCode3() + std::string{'\x42', '\x01'};
    const auto pps265 = StartCode3() + std::string{'\x44', '\x01'};
    EXPECT_FALSE(StreamHelper::HasDecoderConfiguration(true, vps265 + sps265));
    EXPECT_FALSE(StreamHelper::HasDecoderConfiguration(true, vps265 + pps265));
    EXPECT_FALSE(StreamHelper::HasDecoderConfiguration(true, sps265 + pps265));
    EXPECT_TRUE(StreamHelper::HasDecoderConfiguration(true, vps265 + sps265 + pps265));
}

TEST(SdkStreamHelperTest, StartupDropsRecoveryFramesAndThrottlesKeyRequests) {
    DecoderStartupGate gate{};
    const auto start = std::chrono::steady_clock::time_point{};
    using Decision = DecoderStartupGate::Decision;
    EXPECT_EQ(gate.Observe(false, false, start), Decision::kRequestKeyFrame);
    EXPECT_EQ(gate.Observe(true, false, start + std::chrono::milliseconds{100}), Decision::kWait);
    EXPECT_EQ(gate.Observe(false, true, start + std::chrono::milliseconds{500}), Decision::kWait);
    EXPECT_EQ(gate.Observe(false, false, start + std::chrono::seconds{1}), Decision::kRequestKeyFrame);
    EXPECT_EQ(gate.Observe(true, true, start + std::chrono::milliseconds{1001}), Decision::kDecode);
}

TEST(SdkStreamHelperTest, RecreatedDecoderRequiresANewKeyAndMonitorsAreIndependent) {
    using Decision = DecoderStartupGate::Decision;
    const auto start = std::chrono::steady_clock::time_point{};
    DecoderStartupGate first{};
    DecoderStartupGate second{};
    for (int iteration{0}; iteration < 64; ++iteration) {
        EXPECT_EQ(first.Observe(false, false, start), Decision::kRequestKeyFrame);
        EXPECT_EQ(first.Observe(true, true, start), Decision::kDecode);
    }
    EXPECT_EQ(first.Observe(false, false, start), Decision::kRequestKeyFrame);
    EXPECT_EQ(second.Observe(false, false, start), Decision::kRequestKeyFrame);
}

} // namespace
} // namespace px
