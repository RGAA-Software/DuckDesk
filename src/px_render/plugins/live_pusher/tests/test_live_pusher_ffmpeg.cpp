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
#include "px_common_new/data.h"

namespace px {
namespace {

std::shared_ptr<Data> PcmSilence() {
    return Data::Make(std::string(480 * 2 * 2, '\0').data(), 480 * 2 * 2);
}

std::shared_ptr<Data> H264ParameterKeyframe() {
    static constexpr std::array<uint8_t, 31> bytes = {
        0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1f, 0xe5, 0x88, 0x68, 0x54,
        0, 0, 0, 1, 0x68, 0xce, 0x06, 0xe2,
        0, 0, 0, 1, 0x65, 0x88, 0x84, 0x00, 0x10, 0x20, 0x30};
    return Data::Make(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int64_t>(bytes.size()));
}

TEST(LivePusherFfmpegTest, TenFailedOpenAndRepeatedCloseRoundsReleaseResources) {
    for (int round = 0; round < 10; ++round) {
        LivePusherRuntime::Config config;
        config.publish_url = "rtmp://127.0.0.1:1/live/lifecycle";
        config.audio_bitrate = 96000;
        std::atomic<int> keyframe_requests = 0;
        auto processor = MakeFfmpegLivePushProcessor(
            config, [&keyframe_requests] { ++keyframe_requests; });
        ASSERT_TRUE(processor) << "round " << round;
        processor->ProcessVideo(
            H264ParameterKeyframe(), PxPluginEncodedVideoType::kH264,
            640, 480, true, 1);
        processor->ProcessAudio(PcmSilence(), 48000, 2, 16, 2);
        processor->Close();
        processor->Close();
        EXPECT_GE(keyframe_requests.load(), 1) << "round " << round;
    }
}

TEST(LivePusherFfmpegTest, UnsupportedAudioAndMalformedVideoAreHarmless) {
    LivePusherRuntime::Config config;
    config.publish_url = "rtmp://127.0.0.1:1/live/invalid";
    auto processor = MakeFfmpegLivePushProcessor(config, [] {});
    ASSERT_TRUE(processor);
    processor->ProcessAudio(PcmSilence(), 48000, 2, 24, 0);
    processor->ProcessVideo(
        Data::From("not-annex-b"), PxPluginEncodedVideoType::kH264,
        640, 480, true, 1);
    processor->Close();
}

TEST(LivePusherFfmpegTest, RuntimeShutdownIsBoundedDuringFailedConnect) {
    LivePusherRuntime::Config config;
    config.publish_url = "rtmp://127.0.0.1:1/live/shutdown-bound";
    auto runtime = LivePusherRuntime::Make(config, MakeFfmpegLivePushProcessor);
    ASSERT_TRUE(runtime);
    runtime->EnqueueVideo(
        "monitor-1", PxPluginEncodedVideoType::kH264,
        H264ParameterKeyframe(), 640, 480, true, 1);
    runtime->EnqueueAudio(PcmSilence(), 48000, 2, 16, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto started = std::chrono::steady_clock::now();
    runtime->Shutdown();
    EXPECT_LT(std::chrono::steady_clock::now() - started,
              std::chrono::seconds(2));
}

TEST(LivePusherFfmpegTest, LocalRtmpServerPublishesAndClosesWhenConfigured) {
    if (std::getenv("PX_TEST_LOCAL_RTMP") == nullptr) {
        GTEST_SKIP() << "set PX_TEST_LOCAL_RTMP for the local integration test";
    }
    LivePusherRuntime::Config config;
    config.publish_url = std::getenv("PX_TEST_LOCAL_RTMP");
    auto processor = MakeFfmpegLivePushProcessor(config, [] {});
    ASSERT_TRUE(processor);
    processor->ProcessVideo(
        H264ParameterKeyframe(), PxPluginEncodedVideoType::kH264,
        640, 480, true, 1);
    processor->ProcessAudio(PcmSilence(), 48000, 2, 16, 2);
    ASSERT_TRUE(processor->IsPublishing());
    for (int frame = 1; frame <= 10; ++frame) {
        processor->ProcessVideo(
            H264ParameterKeyframe(), PxPluginEncodedVideoType::kH264,
            640, 480, true, 2 + frame * 16);
        processor->ProcessAudio(PcmSilence(), 48000, 2, 16, 2 + frame * 16);
    }
    processor->Close();
    EXPECT_FALSE(processor->IsPublishing());
}

}  // namespace
}  // namespace px
