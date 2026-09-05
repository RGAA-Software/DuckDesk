#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "pipeline/encoded_media_bus.h"
#include "pipeline/media_types.h"
#include "px_common_new/async_operation.h"
#include "px_common_new/async_runtime.h"
#include "sinks/media_recorder_sink.h"

namespace px::render {
namespace {

using namespace std::chrono_literals;

struct WriterCounters final {
    std::atomic_int video{0};
    std::atomic_int audio{0};
    std::atomic_int stopped{0};
};

struct FactoryState final {
    std::mutex mutex;
    std::vector<std::shared_ptr<WriterCounters>> writers;
    std::vector<MediaRecorderSink::KeyframeRequester> retained_callbacks;
};

class FakeRecorderWriter final : public MediaRecorderWriter {
  public:
    explicit FakeRecorderWriter(std::shared_ptr<WriterCounters> counters) : counters_(std::move(counters)) {}

    void OnVideo(const std::shared_ptr<const EncodedVideoFrame>&) override {
        ++counters_->video;
    }

    void OnAudio(const std::shared_ptr<const EncodedAudioFrame>&) override {
        ++counters_->audio;
    }

    void Stop() override {
        ++counters_->stopped;
    }

  private:
    std::shared_ptr<WriterCounters> counters_;
};

MediaRecorderSink::WriterFactory MakeFactory(const std::shared_ptr<FactoryState>& state) {
    return [state](const std::string&, const MediaRecorderOptions&, const MediaRecorderSink::KeyframeRequester& callback) {
        const auto counters = std::make_shared<WriterCounters>();
        {
            std::lock_guard lock(state->mutex);
            state->writers.push_back(counters);
            state->retained_callbacks.push_back(callback);
        }
        return std::make_shared<FakeRecorderWriter>(counters);
    };
}

std::shared_ptr<const EncodedVideoFrame> MakeVideo(const std::uint64_t index) {
    return std::make_shared<const EncodedVideoFrame>(EncodedVideoFrame{
        .identity =
            FrameIdentity{
                .stream_id = "stream-a",
                .monitor_id = "monitor-a",
                .frame_index = index,
                .timestamp_us = index * 1000,
            },
        .codec = "h264",
        .width = 1280,
        .height = 720,
        .key_frame = index == 0,
        .payload = MakeImmutableByteBuffer("encoded-video"),
    });
}

std::shared_ptr<const EncodedAudioFrame> MakeAudio() {
    return std::make_shared<const EncodedAudioFrame>(EncodedAudioFrame{
        .stream_id = "stream-a",
        .timestamp_us = 10,
        .codec = "opus",
        .samples = 960,
        .channels = 2,
        .bits_per_sample = 16,
        .frame_size = 20,
        .payload = MakeImmutableByteBuffer("encoded-audio"),
    });
}

PxAwaitable<void> CompleteStop(std::shared_ptr<MediaRecorderSink> sink, std::shared_ptr<std::promise<ModuleLifecycleResult>> completion) {
    completion->set_value(co_await MediaRecorderSink::StopAsync(sink, std::chrono::steady_clock::now() + 2s));
    co_return;
}

ModuleLifecycleResult StopAndWait(const std::shared_ptr<PxAsyncRuntime>& runtime, const std::shared_ptr<MediaRecorderSink>& sink) {
    const auto completion = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto future = completion->get_future();
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    if (!scope->Spawn("media_recorder_test_stop", [sink, completion] { return CompleteStop(sink, completion); })) {
        return std::unexpected(RenderError{
            .code = RenderErrorCode::kModuleStopFailed,
            .component = "test",
            .operation = "stop",
            .stage = "test",
            .reason = "failed to schedule stop",
        });
    }
    if (future.wait_for(3s) != std::future_status::ready) {
        return std::unexpected(RenderError{
            .code = RenderErrorCode::kAsyncScopeDrainTimeout,
            .component = "test",
            .operation = "stop",
            .stage = "test",
            .reason = "stop future timed out",
        });
    }
    auto result = future.get();
    static_cast<void>(scope->StopAndWait(1s));
    return result;
}

TEST(MediaRecorderSinkTest, TenStartDrainStopRoundsAreComplete) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto media_bus = EncodedMediaBus::Create();
    const auto factory_state = std::make_shared<FactoryState>();
    const auto keyframe_requests = std::make_shared<std::atomic_int>(0);
    auto sink = MediaRecorderSink::Create(
        media_bus, MediaRecorderOptions{.record_directory = "test", .queue_capacity = 128}, [keyframe_requests] { ++(*keyframe_requests); },
        MakeFactory(factory_state));
    ASSERT_TRUE(sink);

    for (int round = 0; round < 10; ++round) {
        ASSERT_TRUE(sink->Start());
        sink->StartRecording();
        for (std::uint64_t index = 0; index < 32; ++index) {
            media_bus->PublishVideo(MakeVideo(index));
            media_bus->PublishEncodedAudio(MakeAudio());
        }
        ASSERT_TRUE(StopAndWait(runtime, sink)) << "round " << round;
    }

    EXPECT_EQ(keyframe_requests->load(), 10);
    std::lock_guard lock(factory_state->mutex);
    ASSERT_EQ(factory_state->writers.size(), 10U);
    for (const auto& counters : factory_state->writers) {
        EXPECT_EQ(counters->video.load(), 32);
        EXPECT_EQ(counters->audio.load(), 32);
        EXPECT_EQ(counters->stopped.load(), 1);
    }
    sink.reset();
    runtime->RequestStop();
    runtime->Join();
}

TEST(MediaRecorderSinkTest, AutoModeTracksFirstAndLastClient) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto media_bus = EncodedMediaBus::Create();
    const auto factory_state = std::make_shared<FactoryState>();
    auto sink = MediaRecorderSink::Create(media_bus,
                                          MediaRecorderOptions{
                                              .record_directory = "test",
                                              .auto_enabled = true,
                                              .queue_capacity = 32,
                                          },
                                          {}, MakeFactory(factory_state));
    ASSERT_TRUE(sink->Start());

    media_bus->PublishClientConnected(MediaClientConnected{.visitor_device_id = "client-a", .stream_id = "stream-a"});
    EXPECT_TRUE(sink->Snapshot().recording);
    media_bus->PublishClientConnected(MediaClientConnected{.visitor_device_id = "client-b", .stream_id = "stream-b"});
    media_bus->PublishClientDisconnected(MediaClientDisconnected{.visitor_device_id = "client-a", .stream_id = "stream-a"});
    EXPECT_TRUE(sink->Snapshot().recording);
    media_bus->PublishClientDisconnected(MediaClientDisconnected{.visitor_device_id = "client-b", .stream_id = "stream-b"});
    EXPECT_FALSE(sink->Snapshot().recording);

    EXPECT_TRUE(StopAndWait(runtime, sink));
    sink.reset();
    runtime->RequestStop();
    runtime->Join();
}

TEST(MediaRecorderSinkTest, RetainedWriterCallbackIsSafeAfterDestruction) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto media_bus = EncodedMediaBus::Create();
    const auto factory_state = std::make_shared<FactoryState>();
    const auto keyframe_requests = std::make_shared<std::atomic_int>(0);
    auto sink = MediaRecorderSink::Create(
        media_bus, MediaRecorderOptions{.record_directory = "test", .queue_capacity = 8}, [keyframe_requests] { ++(*keyframe_requests); },
        MakeFactory(factory_state));
    ASSERT_TRUE(sink->Start());
    sink->StartRecording();
    media_bus->PublishVideo(MakeVideo(0));
    ASSERT_TRUE(StopAndWait(runtime, sink));

    MediaRecorderSink::KeyframeRequester retained;
    {
        std::lock_guard lock(factory_state->mutex);
        ASSERT_EQ(factory_state->retained_callbacks.size(), 1U);
        retained = factory_state->retained_callbacks.front();
    }
    const auto before = keyframe_requests->load();
    sink.reset();
    retained();
    EXPECT_EQ(keyframe_requests->load(), before);
    EXPECT_FALSE(media_bus->NeedsVideo());
    EXPECT_FALSE(media_bus->NeedsEncodedAudio());
    runtime->RequestStop();
    runtime->Join();
}

TEST(MediaRecorderSinkTest, StopRequestedFromKeyframeCallbackDoesNotDeadlock) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto media_bus = EncodedMediaBus::Create();
    const auto factory_state = std::make_shared<FactoryState>();
    const auto weak_holder = std::make_shared<std::weak_ptr<MediaRecorderSink>>();
    auto sink = MediaRecorderSink::Create(
        media_bus, MediaRecorderOptions{.record_directory = "test", .queue_capacity = 8},
        [weak_holder] {
            if (const auto active_sink = weak_holder->lock()) {
                active_sink->StopRecording();
            }
        },
        MakeFactory(factory_state));
    *weak_holder = sink;
    ASSERT_TRUE(sink->Start());
    sink->StartRecording();
    EXPECT_FALSE(sink->Snapshot().recording);
    EXPECT_TRUE(StopAndWait(runtime, sink));
    sink.reset();
    runtime->RequestStop();
    runtime->Join();
}

} // namespace
} // namespace px::render
