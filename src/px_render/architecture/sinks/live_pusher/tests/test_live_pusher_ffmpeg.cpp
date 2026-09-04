#include <array>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "live_pusher_ffmpeg.h"

namespace px::render {
namespace {

std::shared_ptr<const CapturedAudioFrame> PcmSilence() {
    return std::make_shared<const CapturedAudioFrame>(CapturedAudioFrame{
        .timestamp_us = 2000,
        .sample_rate_hz = 48000,
        .channels = 2,
        .bits_per_sample = 16,
        .payload = MakeImmutableByteBuffer(std::string(480 * 2 * 2, '\0')),
    });
}

std::shared_ptr<const EncodedVideoFrame> H264ParameterKeyframe(
    const std::uint64_t timestamp_us = 1000) {
    static constexpr std::array<uint8_t, 31> bytes = {
        0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1f, 0xe5, 0x88, 0x68, 0x54,
        0, 0, 0, 1, 0x68, 0xce, 0x06, 0xe2,
        0, 0, 0, 1, 0x65, 0x88, 0x84, 0x00, 0x10, 0x20, 0x30};
    const auto payload =
        std::make_shared<const std::vector<std::uint8_t>>(
            bytes.begin(), bytes.end());
    return std::make_shared<const EncodedVideoFrame>(EncodedVideoFrame{
        .identity = FrameIdentity{
            .monitor_id = "monitor-1",
            .timestamp_us = timestamp_us,
        },
        .codec = "h264",
        .width = 640,
        .height = 480,
        .key_frame = true,
        .payload = payload,
    });
}

TEST(LivePusherFfmpegTest, TenFailedOpenAndRepeatedCloseRoundsReleaseResources) {
    for (int round = 0; round < 10; ++round) {
        LivePusherOptions config;
        config.publish_url = "rtmp://127.0.0.1:1/live/lifecycle";
        config.audio_bitrate = 96000;
        const auto keyframe_requests = std::make_shared<std::atomic_int>(0);
        auto processor = MakeFfmpegLivePushProcessor(
            config, [keyframe_requests] { ++(*keyframe_requests); });
        ASSERT_TRUE(processor) << "round " << round;
        processor->ProcessVideo(H264ParameterKeyframe());
        processor->ProcessAudio(PcmSilence());
        processor->Close();
        processor->Close();
        EXPECT_GE(keyframe_requests->load(), 1) << "round " << round;
    }
}

TEST(LivePusherFfmpegTest, UnsupportedAudioAndMalformedVideoAreHarmless) {
    LivePusherOptions config;
    config.publish_url = "rtmp://127.0.0.1:1/live/invalid";
    auto processor = MakeFfmpegLivePushProcessor(config, [] {});
    ASSERT_TRUE(processor);
    auto unsupported_audio = PcmSilence();
    auto unsupported_value = *unsupported_audio;
    unsupported_value.bits_per_sample = 24;
    processor->ProcessAudio(
        std::make_shared<const CapturedAudioFrame>(
            std::move(unsupported_value)));
    processor->ProcessVideo(
        std::make_shared<const EncodedVideoFrame>(EncodedVideoFrame{
            .identity = FrameIdentity{.monitor_id = "monitor-1"},
            .codec = "h264",
            .width = 640,
            .height = 480,
            .key_frame = true,
            .payload = MakeImmutableByteBuffer("not-annex-b"),
        }));
    processor->Close();
}

TEST(LivePusherFfmpegTest, CloseIsBoundedDuringFailedConnect) {
    LivePusherOptions config;
    config.publish_url = "rtmp://127.0.0.1:1/live/shutdown-bound";
    auto processor = MakeFfmpegLivePushProcessor(config, [] {});
    ASSERT_TRUE(processor);
    processor->ProcessVideo(H264ParameterKeyframe());
    processor->ProcessAudio(PcmSilence());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto started = std::chrono::steady_clock::now();
    processor->Close();
    EXPECT_LT(std::chrono::steady_clock::now() - started,
              std::chrono::seconds(2));
}

TEST(LivePusherFfmpegTest, LocalRtmpServerPublishesAndClosesWhenConfigured) {
    if (std::getenv("PX_TEST_LOCAL_RTMP") == nullptr) {
        GTEST_SKIP() << "set PX_TEST_LOCAL_RTMP for the local integration test";
    }
    LivePusherOptions config;
    config.publish_url = std::getenv("PX_TEST_LOCAL_RTMP");
    auto processor = MakeFfmpegLivePushProcessor(config, [] {});
    ASSERT_TRUE(processor);
    processor->ProcessVideo(H264ParameterKeyframe());
    processor->ProcessAudio(PcmSilence());
    ASSERT_TRUE(processor->IsPublishing());
    for (int frame = 1; frame <= 10; ++frame) {
        processor->ProcessVideo(H264ParameterKeyframe(
            static_cast<std::uint64_t>(2 + frame * 16) * 1000U));
        processor->ProcessAudio(PcmSilence());
    }
    processor->Close();
    EXPECT_FALSE(processor->IsPublishing());
}

}  // namespace
}  // namespace px::render
