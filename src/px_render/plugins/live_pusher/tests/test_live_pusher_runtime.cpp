#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include <gtest/gtest.h>

#include "live_pusher_runtime.h"
#include "px_common_new/data.h"

namespace px {
namespace {

struct ProcessorState final {
    std::mutex mutex;
    std::condition_variable condition;
    int video_count = 0;
    int audio_count = 0;
    int key_count = 0;
    int close_count = 0;
    bool block = false;
    bool entered = false;
    bool release = false;
    bool request_on_video = false;
    LivePusherRuntime::KeyframeRequester request_keyframe;
};

class FakeProcessor final : public LivePushProcessor {
public:
    explicit FakeProcessor(std::shared_ptr<ProcessorState> state)
        : state_(std::move(state)) {}

    void ProcessVideo(const std::shared_ptr<Data>&,
                      PxPluginEncodedVideoType,
                      int,
                      int,
                      bool key,
                      int64_t) override {
        std::unique_lock lock(state_->mutex);
        ++state_->video_count;
        if (key) {
            ++state_->key_count;
        }
        if (state_->block && !state_->entered) {
            state_->entered = true;
            state_->condition.notify_all();
            state_->condition.wait(lock, [state = state_] {
                return state->release;
            });
        }
        const auto request_keyframe = state_->request_on_video
            ? state_->request_keyframe
            : LivePusherRuntime::KeyframeRequester{};
        lock.unlock();
        if (request_keyframe) {
            request_keyframe();
            state_->condition.notify_all();
        }
    }

    void ProcessAudio(const std::shared_ptr<Data>&,
                      int,
                      int,
                      int,
                      int64_t) override {
        std::lock_guard lock(state_->mutex);
        ++state_->audio_count;
    }

    void Close() override {
        std::lock_guard lock(state_->mutex);
        ++state_->close_count;
    }

    bool IsPublishing() const override { return false; }

private:
    std::shared_ptr<ProcessorState> state_;
};

LivePusherRuntime::ProcessorFactory MakeFactory(
    const std::shared_ptr<ProcessorState>& state) {
    return [state](const LivePusherRuntime::Config&,
                   const LivePusherRuntime::KeyframeRequester& requester) {
        state->request_keyframe = requester;
        return std::make_shared<FakeProcessor>(state);
    };
}

std::shared_ptr<Data> TestData() {
    return Data::From("encoded-packet");
}

TEST(LivePusherRuntimeTest, TenCreateDrainShutdownRounds) {
    for (int round = 0; round < 10; ++round) {
        auto state = std::make_shared<ProcessorState>();
        auto runtime = LivePusherRuntime::Make({}, MakeFactory(state));
        ASSERT_TRUE(runtime) << "round " << round;
        for (int frame = 0; frame < 16; ++frame) {
            runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                                  TestData(), 1920, 1080, frame == 0, frame * 16);
            runtime->EnqueueAudio(TestData(), 48000, 2, 16, frame * 16);
        }
        runtime->Shutdown();
        EXPECT_EQ(state->video_count, 16) << "round " << round;
        EXPECT_EQ(state->audio_count, 16) << "round " << round;
        EXPECT_EQ(state->close_count, 1) << "round " << round;
    }
}

TEST(LivePusherRuntimeTest, SelectedMonitorFiltersOtherScreens) {
    LivePusherRuntime::Config config;
    config.primary_monitor = "monitor-2";
    auto state = std::make_shared<ProcessorState>();
    auto runtime = LivePusherRuntime::Make(config, MakeFactory(state));
    runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                          TestData(), 800, 600, true, 1);
    runtime->EnqueueVideo("monitor-2", PxPluginEncodedVideoType::kH264,
                          TestData(), 800, 600, true, 2);
    runtime->Shutdown();
    EXPECT_EQ(state->video_count, 1);
    EXPECT_EQ(state->key_count, 1);
}

TEST(LivePusherRuntimeTest, QueueKeepsNewKeyframeByEvictingNonKeyFrame) {
    auto state = std::make_shared<ProcessorState>();
    state->block = true;
    auto runtime = LivePusherRuntime::Make({}, MakeFactory(state));
    runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                          TestData(), 800, 600, false, 0);
    {
        std::unique_lock lock(state->mutex);
        state->condition.wait(lock, [state] { return state->entered; });
    }
    for (int frame = 0; frame < 48; ++frame) {
        runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                              TestData(), 800, 600, false, frame + 1);
    }
    runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                          TestData(), 800, 600, true, 100);
    {
        std::lock_guard lock(state->mutex);
        state->release = true;
    }
    state->condition.notify_all();
    runtime->Shutdown();
    EXPECT_EQ(state->video_count, 49);
    EXPECT_EQ(state->key_count, 1);
}

TEST(LivePusherRuntimeTest, RetainedProcessorCallbackExpiresAfterShutdown) {
    auto state = std::make_shared<ProcessorState>();
    auto runtime = LivePusherRuntime::Make({}, MakeFactory(state));
    std::atomic<int> requests = 0;
    runtime->SetKeyframeRequester([&requests] { ++requests; });
    state->request_keyframe();
    EXPECT_EQ(requests.load(), 1);
    runtime->Shutdown();
    state->request_keyframe();
    EXPECT_EQ(requests.load(), 1);
}

TEST(LivePusherRuntimeTest, ShutdownFromProcessorCallbackDoesNotSelfJoin) {
    auto state = std::make_shared<ProcessorState>();
    state->request_on_video = true;
    auto runtime = LivePusherRuntime::Make({}, MakeFactory(state));
    const auto weak_runtime = std::weak_ptr<LivePusherRuntime>(runtime);
    runtime->SetKeyframeRequester([weak_runtime] {
        if (const auto locked = weak_runtime.lock()) {
            locked->Shutdown();
        }
    });
    runtime->EnqueueVideo("monitor-1", PxPluginEncodedVideoType::kH264,
                          TestData(), 800, 600, true, 0);
    {
        std::unique_lock lock(state->mutex);
        state->condition.wait_for(lock, std::chrono::seconds(2), [runtime] {
            return !runtime->IsAccepting();
        });
    }
    runtime->Shutdown();
    EXPECT_FALSE(runtime->IsAccepting());
    EXPECT_EQ(state->close_count, 1);
}

TEST(LivePusherRuntimeTest, RepeatedShutdownIsIdempotent) {
    auto state = std::make_shared<ProcessorState>();
    auto runtime = LivePusherRuntime::Make({}, MakeFactory(state));
    runtime->Shutdown();
    runtime->Shutdown();
    EXPECT_EQ(state->close_count, 1);
}

}  // namespace
}  // namespace px
