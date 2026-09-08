#include "sdk_video_decoder_factory.h"
#include "thunder_sdk.h"
#include "platform/windows/windows_decoder_factory.h"
#include "platform/windows/windows_video_resources.h"
#include "av_buffer_ref.h"
#include "sdk_ffmpeg_decoder.h"
#include "sdk_ffmpeg_soft_decoder.h"
#include "sdk_ffmpeg_vulkan_decoder.h"
#include "px_common/message_notifier.h"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>

namespace px {
namespace {

using namespace std::chrono_literals;

class TestDecoderFactory final : public VideoDecoderFactory {
  public:
    VideoDecoderCreation Create(const std::shared_ptr<ThunderSdk>&, const VideoFrame&, bool) override {
        return {};
    }
    bool SupportsMultipleStreams() const noexcept override {
        return false;
    }
};

bool WaitFor(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

struct SdkHarness final {
    std::shared_ptr<MessageNotifier> notifier{std::make_shared<MessageNotifier>()};
    std::shared_ptr<ThunderSdk> sdk{ThunderSdk::Make(notifier)};

    ~SdkHarness() {
        if (sdk)
            sdk->Exit();
        notifier->Stop(MessageBusStopMode::kCancel);
    }

    bool Initialize(std::shared_ptr<VideoDecoderFactory> factory) {
        const auto params = std::make_shared<ThunderSdkParams>();
        params->ip_ = "127.0.0.1";
        params->port_ = 9;
        params->file_transfer_only_ = true;
        params->ft_path_ = "/file/transfer?decoder-lifecycle-test=1";
        return sdk->Init(params, std::move(factory));
    }
};

TEST(SdkDecoderFactory, RequiresInjectedFactoryAndRejectsReinitialization) {
    SdkHarness harness{};
    EXPECT_FALSE(harness.Initialize({}));
    harness.sdk->Start(); // Partial/failed initialization must be safe.
    EXPECT_TRUE(harness.Initialize(std::make_shared<TestDecoderFactory>()));
    EXPECT_FALSE(harness.Initialize(std::make_shared<TestDecoderFactory>()));
    harness.sdk->Exit();
    harness.sdk->Exit();
    EXPECT_FALSE(harness.Initialize(std::make_shared<TestDecoderFactory>()));
    harness.sdk->Start(); // A stopped session cannot be resurrected.
}

TEST(SdkDecoderFactory, OutputRefreshBeforeStartCompletesAndExitReleasesFactory) {
    SdkHarness harness{};
    auto factory = std::make_shared<TestDecoderFactory>();
    const std::weak_ptr<VideoDecoderFactory> weak_factory = factory;
    ASSERT_TRUE(harness.Initialize(std::move(factory)));
    const auto completed = std::make_shared<std::atomic_int>(0);
    harness.sdk->RefreshVideoOutput(true, [completed] { ++*completed; });
    EXPECT_EQ(completed->load(), 1);
    EXPECT_FALSE(weak_factory.expired());
    harness.sdk->Exit();
    EXPECT_TRUE(weak_factory.expired());
    harness.sdk->RefreshVideoOutput(false, [completed] { ++*completed; });
    EXPECT_EQ(completed->load(), 2);
}

TEST(SdkDecoderFactory, OutputCallbackCanStopAndDestroySessionRepeatedly) {
    for (int iteration{0}; iteration < 3; ++iteration) {
        SdkHarness harness{};
        ASSERT_TRUE(harness.Initialize(std::make_shared<TestDecoderFactory>()));
        harness.sdk->Start();
        harness.sdk->Start();
        const std::weak_ptr<ThunderSdk> weak_sdk = harness.sdk;
        const auto completed = std::make_shared<std::atomic_bool>(false);
        harness.sdk->RefreshVideoOutput(true, [weak_sdk, completed] {
            if (const auto sdk = weak_sdk.lock())
                sdk->Exit();
            *completed = true;
        });
        ASSERT_TRUE(WaitFor([completed] { return completed->load(); }));
        harness.sdk.reset();
        EXPECT_TRUE(WaitFor([weak_sdk] { return weak_sdk.expired(); }));
    }
}

TEST(SdkDecoderFactory, ShutdownDiscardsQueuedOutputOwnerAfterWorkerStops) {
    SdkHarness harness{};
    ASSERT_TRUE(harness.Initialize(std::make_shared<TestDecoderFactory>()));
    harness.sdk->Start();
    const auto entered = std::make_shared<std::promise<void>>();
    const auto started = entered->get_future();
    harness.sdk->PostVideoTask(
        [entered] {
            entered->set_value();
            std::this_thread::sleep_for(250ms);
        },
        0, "lifecycle-test");
    ASSERT_EQ(started.wait_for(3s), std::future_status::ready);
    auto output_owner = std::make_shared<int>(42);
    const std::weak_ptr<int> weak_output = output_owner;
    harness.sdk->RefreshVideoOutput(true, [output_owner] { static_cast<void>(output_owner); });
    output_owner.reset();
    harness.sdk->Exit();
    EXPECT_TRUE(WaitFor([weak_output] { return weak_output.expired(); }));
}

TEST(SdkDecoderFactory, WindowsFactoryIsConcreteAndRejectsMissingSession) {
    EXPECT_FALSE(MakeWindowsVideoDecoderFactory({}));
    const auto factory = MakeWindowsVideoDecoderFactory(std::make_shared<WindowsVideoResources>());
    ASSERT_TRUE(factory);
    EXPECT_TRUE(factory->SupportsMultipleStreams());
    EXPECT_FALSE(factory->Create({}, VideoFrame{}, false).decoder);
}

TEST(SdkDecoderFactory, RealDecoderCleanupIsSafeBeforeInitAndAfterUnsupportedCodec) {
    SdkHarness harness{};
    ASSERT_TRUE(harness.Initialize(std::make_shared<TestDecoderFactory>()));
    const auto resources = std::make_shared<WindowsVideoResources>();
    const std::vector<std::shared_ptr<VideoDecoder>> decoders{std::make_shared<FFmpegDecoder>(harness.sdk, resources),
                                                              std::make_shared<FFmpegVulkanDecoder>(harness.sdk, resources),
                                                              std::make_shared<FFmpegVideoDecoder>(harness.sdk)};
    for (const auto& decoder : decoders) {
        decoder->Release();
        EXPECT_NE(decoder->Init("failure-test", -1, 16, 16, {}, 0, true), 0);
        decoder->Release();
        decoder->Release();
        EXPECT_FALSE(decoder->Ready());
    }
}

TEST(SdkDecoderFactory, WindowsFactoryRetainsResourcesUntilSessionStops) {
    SdkHarness harness{};
    auto resources = std::make_shared<WindowsVideoResources>();
    const std::weak_ptr<const WindowsVideoResources> weak_resources = resources;
    ASSERT_TRUE(harness.Initialize(MakeWindowsVideoDecoderFactory(resources)));
    resources.reset();
    EXPECT_FALSE(weak_resources.expired());
    harness.sdk->Exit();
    EXPECT_TRUE(weak_resources.expired());
}

TEST(SdkDecoderFactory, BufferSnapshotKeepsBackingResourceAlive) {
    auto source = AvBufferPtr(av_buffer_alloc(16), AvBufferDeleter{});
    ASSERT_TRUE(source);
    source->data[0] = 42;
    auto snapshot = CloneAvBuffer(*source);
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(av_buffer_get_ref_count(source.get()), 2);
    source.reset();
    EXPECT_EQ(snapshot->data[0], 42);
    EXPECT_EQ(av_buffer_get_ref_count(snapshot.get()), 1);
}

TEST(SdkDecoderFactory, DetachedOutputDropsDisplayWorkAndConfigurePrecedesCompletion) {
    SdkHarness harness{};
    ASSERT_TRUE(harness.Initialize(std::make_shared<TestDecoderFactory>()));
    harness.sdk->Start();
    const auto phase = std::make_shared<std::atomic_int>(0);
    harness.sdk->RefreshVideoOutput(
        false,
        [phase] {
            if (phase->load() == 1)
                *phase = 2;
        },
        [phase] { *phase = 1; });
    ASSERT_TRUE(WaitFor([phase] { return phase->load() == 2; }));
    auto queued_owner = std::make_shared<int>(42);
    const std::weak_ptr<int> weak_owner = queued_owner;
    harness.sdk->PostVideoTask([queued_owner, phase] { *phase = -1; }, 1, "detached-output");
    queued_owner.reset();
    EXPECT_TRUE(weak_owner.expired());
    EXPECT_EQ(phase->load(), 2);
    harness.sdk->RefreshVideoOutput(
        true,
        [phase] {
            if (phase->load() == 3)
                *phase = 4;
        },
        [phase] { *phase = 3; });
    ASSERT_TRUE(WaitFor([phase] { return phase->load() == 4; }));
    harness.sdk->PostVideoTask([phase] { *phase = 5; }, 2, "reattached-output");
    ASSERT_TRUE(WaitFor([phase] { return phase->load() == 5; }));
    harness.sdk->Exit();
    harness.sdk->RefreshVideoOutput(true, {}, [phase] { *phase = -1; });
    EXPECT_EQ(phase->load(), 5);
}

} // namespace
} // namespace px
