#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "audio_capture.h"
#include "px_common/data.h"
#include "sources/was_audio_capture_source.h"
#include "was_audio_capture_runtime.h"

namespace px::render {
namespace {

using namespace std::chrono_literals;

class FakeAudioCapture final : public IAudioCapture {
public:
    int Start() override {
        running_ = true;
        if (const auto callback = SnapshotCallbacks().format) {
            callback(48000, 2, 16);
        }
        return 0;
    }

    int Pause() override {
        running_ = false;
        return 0;
    }

    int Stop() override {
        running_ = false;
        if (const auto callback = SnapshotCallbacks().stop) {
            callback();
        }
        return 0;
    }

    void Emit(std::string bytes) {
        if (const auto callback = SnapshotCallbacks().data) {
            callback(Data::From(bytes));
        }
    }

    [[nodiscard]] bool Running() const noexcept {
        return running_.load();
    }

private:
    std::atomic_bool running_{false};
};

// Keep shared ownership explicit without storing a back-reference in the
// factory closure.
class SharedRuntimeHarness final
    : public std::enable_shared_from_this<SharedRuntimeHarness> {
public:
    std::shared_ptr<WasAudioCaptureRuntime> MakeRuntime() {
        const auto state = shared_from_this();
        return WasAudioCaptureRuntime::Make(
            [state](std::uint32_t) -> AudioCapturePtr {
                auto capture = std::make_shared<FakeAudioCapture>();
                std::lock_guard lock(state->mutex_);
                state->captures_.push_back(capture);
                return capture;
            },
            [](std::uint32_t) { return true; },
            5ms);
    }

    [[nodiscard]] std::shared_ptr<FakeAudioCapture> Latest() const {
        std::lock_guard lock(mutex_);
        return captures_.empty() ? std::shared_ptr<FakeAudioCapture>{}
                                 : captures_.back();
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<FakeAudioCapture>> captures_;
};

TEST(WasAudioCaptureSourceTest, PublishesTypedFramesAndCanStopFromCallback) {
    const auto harness = std::make_shared<SharedRuntimeHarness>();
    const auto deliveries = std::make_shared<std::atomic_uint64_t>(0);
    const auto source_slot =
        std::make_shared<std::weak_ptr<WasAudioCaptureSource>>();
    const auto source = WasAudioCaptureSource::Create(
        [deliveries, source_slot](const CaptureAudioFrame& frame) {
            EXPECT_EQ(frame.samples_, 48000U);
            EXPECT_EQ(frame.channels_, 2U);
            EXPECT_EQ(frame.bits_, 16U);
            EXPECT_TRUE(frame.full_data_);
            ++(*deliveries);
            if (const auto active_source = source_slot->lock()) {
                active_source->StopProviding();
            }
        },
        [harness] { return harness->MakeRuntime(); });
    *source_slot = source;

    ASSERT_TRUE(source->Start());
    source->SetLoopbackProcessId(42);
    source->StartProviding();
    const auto capture = harness->Latest();
    ASSERT_TRUE(capture);
    ASSERT_TRUE(capture->Running());

    capture->Emit("pcm-frame");
    EXPECT_EQ(deliveries->load(), 1U);
    EXPECT_FALSE(source->IsProviding());
    EXPECT_EQ(source->Snapshot().delivered_frames, 1U);
    EXPECT_TRUE(source->Stop());
}

TEST(WasAudioCaptureSourceTest, DisableAndDestructionInvalidateCallbacks) {
    for (int round = 0; round < 10; ++round) {
        const auto harness = std::make_shared<SharedRuntimeHarness>();
        const auto deliveries = std::make_shared<std::atomic_uint64_t>(0);
        auto source = WasAudioCaptureSource::Create(
            [deliveries](const CaptureAudioFrame&) { ++(*deliveries); },
            [harness] { return harness->MakeRuntime(); });
        ASSERT_TRUE(source->Start());
        source->StartProviding();
        const auto capture = harness->Latest();
        ASSERT_TRUE(capture);

        ASSERT_TRUE(source->SetEnabled(false));
        capture->Emit("disabled");
        EXPECT_EQ(deliveries->load(), 0U);
        EXPECT_FALSE(source->IsProviding());

        ASSERT_TRUE(source->Stop());
        source.reset();
        capture->Emit("destroyed");
        EXPECT_EQ(deliveries->load(), 0U);
    }
}

TEST(WasAudioCaptureSourceTest, RepeatedLifecycleIsIdempotent) {
    const auto harness = std::make_shared<SharedRuntimeHarness>();
    const auto source = WasAudioCaptureSource::Create(
        [](const CaptureAudioFrame&) {},
        [harness] { return harness->MakeRuntime(); });

    for (int round = 0; round < 10; ++round) {
        ASSERT_TRUE(source->Start());
        ASSERT_TRUE(source->Start());
        source->StartProviding();
        EXPECT_TRUE(source->IsProviding());
        ASSERT_TRUE(source->Stop());
        ASSERT_TRUE(source->Stop());
        EXPECT_FALSE(source->IsProviding());
    }
}

}  // namespace
}  // namespace px::render
