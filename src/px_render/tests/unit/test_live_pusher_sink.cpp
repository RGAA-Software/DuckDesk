#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>

#include "pipeline/encoded_media_bus.h"
#include "pipeline/media_types.h"
#include "px_common_new/async_operation.h"
#include "px_common_new/async_runtime.h"
#include "sinks/live_pusher_sink.h"

namespace px::render {
namespace {

using namespace std::chrono_literals;

struct ProcessorState final {
    std::mutex mutex;
    std::condition_variable condition;
    int video_count{0};
    int audio_count{0};
    int key_count{0};
    int close_count{0};
    bool block{false};
    bool entered{false};
    bool release{false};
    bool request_on_video{false};
    LivePusherSink::KeyframeRequester request_keyframe;
};

class FakeLivePushProcessor final : public LivePushProcessor {
  public:
    explicit FakeLivePushProcessor(std::shared_ptr<ProcessorState> state) : state_(std::move(state)) {}

    void ProcessVideo(const std::shared_ptr<const EncodedVideoFrame>& frame) override {
        std::unique_lock lock(state_->mutex);
        ++state_->video_count;
        if (frame && frame->key_frame) {
            ++state_->key_count;
        }
        if (state_->block && !state_->entered) {
            state_->entered = true;
            state_->condition.notify_all();
            state_->condition.wait(lock, [state = state_] { return state->release; });
        }
        const auto callback = state_->request_on_video ? state_->request_keyframe : LivePusherSink::KeyframeRequester{};
        lock.unlock();
        if (callback) {
            callback();
        }
    }

    void ProcessAudio(const std::shared_ptr<const CapturedAudioFrame>&) override {
        std::lock_guard lock(state_->mutex);
        ++state_->audio_count;
    }

    bool IsPublishing() const override {
        return false;
    }

    void Close() override {
        std::lock_guard lock(state_->mutex);
        ++state_->close_count;
    }

  private:
    std::shared_ptr<ProcessorState> state_;
};

LivePusherSink::ProcessorFactory MakeFactory(const std::shared_ptr<ProcessorState>& state) {
    return [state](const LivePusherOptions&, const LivePusherSink::KeyframeRequester& callback) {
        state->request_keyframe = callback;
        return std::make_shared<FakeLivePushProcessor>(state);
    };
}

std::shared_ptr<const EncodedVideoFrame> MakeVideo(const std::uint64_t index, const std::string& monitor = "monitor-1") {
    return std::make_shared<const EncodedVideoFrame>(EncodedVideoFrame{
        .identity =
            FrameIdentity{
                .monitor_id = monitor,
                .frame_index = index,
                .timestamp_us = index * 16000,
            },
        .codec = "h264",
        .width = 1280,
        .height = 720,
        .key_frame = index == 0,
        .payload = MakeImmutableByteBuffer("video"),
    });
}

std::shared_ptr<const CapturedAudioFrame> MakeAudio() {
    return std::make_shared<const CapturedAudioFrame>(CapturedAudioFrame{
        .timestamp_us = 1000,
        .sample_rate_hz = 48000,
        .channels = 2,
        .bits_per_sample = 16,
        .payload = MakeImmutableByteBuffer("audio"),
    });
}

PxAwaitable<void> CompleteStop(std::shared_ptr<LivePusherSink> sink, std::shared_ptr<std::promise<ModuleLifecycleResult>> completion) {
    completion->set_value(co_await LivePusherSink::StopAsync(sink, std::chrono::steady_clock::now() + 2s));
    co_return;
}

ModuleLifecycleResult StopAndWait(const std::shared_ptr<PxAsyncRuntime>& runtime, const std::shared_ptr<LivePusherSink>& sink) {
    const auto completion = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto future = completion->get_future();
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    if (!scope->Spawn("live_pusher_test_stop", [sink, completion] { return CompleteStop(sink, completion); }) ||
        future.wait_for(3s) != std::future_status::ready) {
        return std::unexpected(RenderError{
            .code = RenderErrorCode::kAsyncScopeDrainTimeout,
            .component = "test",
            .operation = "stop",
            .stage = "test",
            .reason = "stop timed out",
        });
    }
    auto result = future.get();
    static_cast<void>(scope->StopAndWait(1s));
    return result;
}

TEST(LivePusherSinkTest, TenCreateDrainShutdownRounds) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    for (int round = 0; round < 10; ++round) {
        const auto bus = EncodedMediaBus::Create();
        const auto state = std::make_shared<ProcessorState>();
        auto sink = LivePusherSink::Create(bus,
                                           LivePusherOptions{
                                               .enabled = true,
                                               .publish_url = "rtmp://unit.test/live",
                                           },
                                           {}, MakeFactory(state));
        ASSERT_TRUE(sink->Start());
        for (std::uint64_t index = 0; index < 16; ++index) {
            bus->PublishVideo(MakeVideo(index));
            bus->PublishCapturedAudio(MakeAudio());
        }
        ASSERT_TRUE(StopAndWait(runtime, sink)) << "round " << round;
        EXPECT_EQ(state->video_count, 16);
        EXPECT_EQ(state->audio_count, 16);
        EXPECT_EQ(state->close_count, 1);
    }
    runtime->RequestStop();
    runtime->Join();
}

TEST(LivePusherSinkTest, SelectedMonitorFiltersOtherScreens) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto bus = EncodedMediaBus::Create();
    const auto state = std::make_shared<ProcessorState>();
    auto sink = LivePusherSink::Create(bus,
                                       LivePusherOptions{
                                           .enabled = true,
                                           .publish_url = "rtmp://unit.test/live",
                                           .primary_monitor = "monitor-2",
                                       },
                                       {}, MakeFactory(state));
    ASSERT_TRUE(sink->Start());
    bus->PublishVideo(MakeVideo(0, "monitor-1"));
    bus->PublishVideo(MakeVideo(0, "monitor-2"));
    ASSERT_TRUE(StopAndWait(runtime, sink));
    EXPECT_EQ(state->video_count, 1);
    EXPECT_EQ(state->key_count, 1);
    runtime->RequestStop();
    runtime->Join();
}

TEST(LivePusherSinkTest, QueueKeepsNewKeyframeByEvictingNonKeyFrame) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto bus = EncodedMediaBus::Create();
    const auto state = std::make_shared<ProcessorState>();
    state->block = true;
    auto sink = LivePusherSink::Create(bus,
                                       LivePusherOptions{
                                           .enabled = true,
                                           .publish_url = "rtmp://unit.test/live",
                                           .queue_capacity = 48,
                                       },
                                       {}, MakeFactory(state));
    ASSERT_TRUE(sink->Start());
    bus->PublishVideo(MakeVideo(1));
    {
        std::unique_lock lock(state->mutex);
        state->condition.wait(lock, [state] { return state->entered; });
    }
    for (std::uint64_t index = 1; index <= 48; ++index) {
        bus->PublishVideo(MakeVideo(index));
    }
    bus->PublishVideo(MakeVideo(0));
    {
        std::lock_guard lock(state->mutex);
        state->release = true;
    }
    state->condition.notify_all();
    ASSERT_TRUE(StopAndWait(runtime, sink));
    EXPECT_EQ(state->video_count, 49);
    EXPECT_EQ(state->key_count, 1);
    EXPECT_EQ(sink->Snapshot().dropped_media, 1U);
    runtime->RequestStop();
    runtime->Join();
}

TEST(LivePusherSinkTest, DisableFromProcessorCallbackDoesNotSelfJoin) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto bus = EncodedMediaBus::Create();
    const auto state = std::make_shared<ProcessorState>();
    state->request_on_video = true;
    const auto weak_holder = std::make_shared<std::weak_ptr<LivePusherSink>>();
    const auto disabled = std::make_shared<std::atomic_bool>(false);
    auto sink = LivePusherSink::Create(
        bus,
        LivePusherOptions{
            .enabled = true,
            .publish_url = "rtmp://unit.test/live",
        },
        [weak_holder, disabled, state] {
            if (const auto active = weak_holder->lock()) {
                static_cast<void>(active->SetEnabled(false));
                disabled->store(true, std::memory_order_release);
                state->condition.notify_all();
            }
        },
        MakeFactory(state));
    *weak_holder = sink;
    ASSERT_TRUE(sink->Start());
    bus->PublishVideo(MakeVideo(0));
    {
        std::unique_lock lock(state->mutex);
        ASSERT_TRUE(state->condition.wait_for(lock, 2s, [disabled] { return disabled->load(std::memory_order_acquire); }));
    }
    ASSERT_TRUE(StopAndWait(runtime, sink));
    EXPECT_FALSE(bus->NeedsVideo());
    EXPECT_FALSE(bus->NeedsCapturedAudio());
    runtime->RequestStop();
    runtime->Join();
}

} // namespace
} // namespace px::render
