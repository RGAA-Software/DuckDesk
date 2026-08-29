#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "media_recorder_runtime.h"
#include "px_common_new/data.h"

namespace px {
namespace {

struct WriterStats final {
    std::atomic<int> video_count = 0;
    std::atomic<int> audio_count = 0;
    std::atomic<int> stop_count = 0;
};

struct FactoryState final {
    std::mutex mutex;
    std::vector<std::shared_ptr<WriterStats>> writers;
    std::vector<MediaRecorderRuntime::KeyframeRequester> keyframe_callbacks;
};

class FakeWriter final : public MediaRecordWriter {
public:
    explicit FakeWriter(std::shared_ptr<WriterStats> stats)
        : stats_(std::move(stats)) {}

    void OnVideo(const std::shared_ptr<Data>&,
                 PxPluginEncodedVideoType,
                 int,
                 int,
                 bool) override {
        ++stats_->video_count;
    }

    void OnAudio(const std::shared_ptr<Data>&) override {
        ++stats_->audio_count;
    }

    void Stop() override { ++stats_->stop_count; }

private:
    std::shared_ptr<WriterStats> stats_;
};

MediaRecorderRuntime::WriterFactory MakeFactory(
    const std::shared_ptr<FactoryState>& state) {
    return [state](const std::string&,
                   const MediaRecorderRuntime::Config&,
                   const MediaRecorderRuntime::KeyframeRequester& request_keyframe) {
        auto stats = std::make_shared<WriterStats>();
        {
            std::lock_guard lock(state->mutex);
            state->writers.push_back(stats);
            state->keyframe_callbacks.push_back(request_keyframe);
        }
        return std::make_shared<FakeWriter>(std::move(stats));
    };
}

std::shared_ptr<Data> TestData() {
    return Data::From("encoded-packet");
}

TEST(MediaRecorderRuntimeTest, TenStartDrainStopRoundsAreComplete) {
    auto state = std::make_shared<FactoryState>();
    auto runtime = MediaRecorderRuntime::Make({}, MakeFactory(state));
    std::atomic<int> keyframe_requests = 0;
    runtime->SetKeyframeRequester([&keyframe_requests] {
        ++keyframe_requests;
    });

    for (int round = 0; round < 10; ++round) {
        runtime->StartRecord();
        for (int frame = 0; frame < 32; ++frame) {
            runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                                  TestData(), frame, 1920, 1080, frame == 0);
            runtime->EnqueueAudio(TestData());
        }
        runtime->StopRecord();
        EXPECT_FALSE(runtime->IsRecording()) << "round " << round;

        std::shared_ptr<WriterStats> stats;
        {
            std::lock_guard lock(state->mutex);
            ASSERT_EQ(state->writers.size(), static_cast<size_t>(round + 1));
            stats = state->writers.back();
        }
        EXPECT_EQ(stats->video_count.load(), 32) << "round " << round;
        EXPECT_EQ(stats->audio_count.load(), 32) << "round " << round;
        EXPECT_EQ(stats->stop_count.load(), 1) << "round " << round;
    }
    EXPECT_EQ(keyframe_requests.load(), 10);
    runtime->Shutdown();
}

TEST(MediaRecorderRuntimeTest, ShutdownDrainsQueuedMediaAndClosesWriter) {
    auto state = std::make_shared<FactoryState>();
    auto runtime = MediaRecorderRuntime::Make({}, MakeFactory(state));
    runtime->StartRecord();
    for (int frame = 0; frame < 200; ++frame) {
        runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                              TestData(), frame, 1280, 720, frame == 0);
    }

    runtime->Shutdown();
    ASSERT_EQ(state->writers.size(), 1u);
    EXPECT_EQ(state->writers.front()->video_count.load(), 200);
    EXPECT_EQ(state->writers.front()->stop_count.load(), 1);
}

TEST(MediaRecorderRuntimeTest, AutoRecordingTracksFirstAndLastClient) {
    MediaRecorderRuntime::Config config;
    config.auto_enabled = true;
    auto state = std::make_shared<FactoryState>();
    auto runtime = MediaRecorderRuntime::Make(config, MakeFactory(state));

    runtime->OnClientConnected("client-a", "stream-a");
    EXPECT_TRUE(runtime->IsRecording());
    runtime->OnClientConnected("client-b", "stream-b");
    runtime->OnClientDisconnected("client-a", "stream-a");
    EXPECT_TRUE(runtime->IsRecording());
    runtime->OnClientDisconnected("client-b", "stream-b");
    EXPECT_FALSE(runtime->IsRecording());
    runtime->Shutdown();
}

TEST(MediaRecorderRuntimeTest, RetainedWriterCallbackIsHarmlessAfterShutdown) {
    auto state = std::make_shared<FactoryState>();
    auto runtime = MediaRecorderRuntime::Make({}, MakeFactory(state));
    std::atomic<int> requests = 0;
    runtime->SetKeyframeRequester([&requests] { ++requests; });
    runtime->StartRecord();
    runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                          TestData(), 1, 640, 480, true);
    runtime->StopRecord();

    MediaRecorderRuntime::KeyframeRequester retained_callback;
    {
        std::lock_guard lock(state->mutex);
        ASSERT_EQ(state->keyframe_callbacks.size(), 1u);
        retained_callback = state->keyframe_callbacks.front();
    }
    EXPECT_EQ(requests.load(), 1);
    runtime->Shutdown();
    retained_callback();
    EXPECT_EQ(requests.load(), 1);
}

TEST(MediaRecorderRuntimeTest, RepeatedStopAndShutdownAreIdempotent) {
    auto state = std::make_shared<FactoryState>();
    auto runtime = MediaRecorderRuntime::Make({}, MakeFactory(state));
    runtime->StartRecord();
    runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                          TestData(), 1, 640, 480, true);
    runtime->StopRecord();
    runtime->StopRecord();
    runtime->Shutdown();
    runtime->Shutdown();
    ASSERT_EQ(state->writers.size(), 1u);
    EXPECT_EQ(state->writers.front()->stop_count.load(), 1);
}

}  // namespace
}  // namespace px
