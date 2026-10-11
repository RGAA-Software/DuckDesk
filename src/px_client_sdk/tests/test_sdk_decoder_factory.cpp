#include "sdk_video_decoder_factory.h"
#include "thunder_sdk.h"
#include "platform/windows/windows_decoder_factory.h"
#include "platform/windows/windows_video_resources.h"
#include "av_buffer_ref.h"
#include "sdk_ffmpeg_decoder.h"
#include "sdk_ffmpeg_soft_decoder.h"
#include "sdk_ffmpeg_vulkan_decoder.h"
#include "px_common/message_notifier.h"
#include "gl/raw_image.h"

#include <asio2/websocket/ws_server.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
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

TEST(SdkDecoderFactory, HostIdentityIsPreservedWithoutDesktopDiscovery) {
    SdkHarness harness{};
    const auto params = std::make_shared<ThunderSdkParams>();
    params->device_name_ = "injected-device-identity";
    params->client_type_ = ClientType::kAndroid;
    ASSERT_TRUE(harness.sdk->Init(params, std::make_shared<TestDecoderFactory>()));
    EXPECT_EQ(harness.sdk->GetSdkParams()->device_name_, "injected-device-identity");
    EXPECT_EQ(harness.sdk->GetSdkParams()->client_type_, ClientType::kAndroid);
}

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
        EXPECT_NE(decoder->Init("failure-test", static_cast<VideoType>(-1), 16, 16, {}, EImageFormat::kI420, true), 0);
        EXPECT_NE(decoder->Init("unsupported-codec", VideoType::kNetVp9, 16, 16, {}, EImageFormat::kI420, true), 0);
        EXPECT_NE(decoder->Init("invalid-format", VideoType::kNetH264, 16, 16, {}, static_cast<EImageFormat>(99), true), 0);
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

struct DecoderRecoveryObservations final {
    std::atomic_int creations{0};
    std::atomic_int releases{0};
    std::atomic_int images{0};
    std::atomic_int key_requests{0};
    std::atomic_int received_frames{0};
    std::atomic_bool withhold_output{false};
    std::mutex session_mutex{};
    std::shared_ptr<asio2::ws_session> session{};
};

class WithheldOutputDecoder final : public VideoDecoder {
  public:
    WithheldOutputDecoder(const std::shared_ptr<ThunderSdk>& sdk, std::shared_ptr<DecoderRecoveryObservations> observations,
                          bool first_decoder)
        : VideoDecoder(sdk), observations_(std::move(observations)), first_decoder_(first_decoder) {}

    Result<std::shared_ptr<RawImage>, int> Decode(std::span<const std::uint8_t>) override {
        if (first_decoder_ && observations_->withhold_output.load()) return TRError(0);
        return RawImage::Make(kRawImageI420, 16, 16);
    }
    bool Ready() override { return true; }
    bool NeedReConstruct(VideoType, int, int, EImageFormat) override { return false; }
    void Release() override {
        if (!released_) ++observations_->releases;
        released_ = true;
        sdk_.reset();
    }

  private:
    std::shared_ptr<DecoderRecoveryObservations> observations_{};
    bool first_decoder_{};
    bool released_{};
};

class WithheldOutputFactory final : public VideoDecoderFactory {
  public:
    explicit WithheldOutputFactory(std::shared_ptr<DecoderRecoveryObservations> observations) : observations_(std::move(observations)) {}
    VideoDecoderCreation Create(const std::shared_ptr<ThunderSdk>& sdk, const VideoFrame&, bool) override {
        const bool first_decoder = observations_->creations.fetch_add(1) == 0;
        return {.decoder = std::make_shared<WithheldOutputDecoder>(sdk, observations_, first_decoder)};
    }
    bool SupportsMultipleStreams() const noexcept override { return true; }

  private:
    std::shared_ptr<DecoderRecoveryObservations> observations_{};
};

TEST(SdkDecoderFactory, SustainedAcceptedInputsRecoverThroughRealVideoQueueAndConfiguredIdr) {
    const auto observations = std::make_shared<DecoderRecoveryObservations>();
    const auto server = std::make_shared<asio2::ws_server>();
    const std::weak_ptr<DecoderRecoveryObservations> weak_observations = observations;
    server->bind_upgrade([weak_observations](const std::shared_ptr<asio2::ws_session>& session) {
        if (const auto state = weak_observations.lock()) {
            session->ws_stream().binary(true);
            std::lock_guard lock(state->session_mutex);
            state->session = session;
        }
    });
    server->bind_recv([weak_observations](const std::shared_ptr<asio2::ws_session>&, std::string_view payload) {
        Message message{};
        if (message.ParseFromString(std::string(payload)) && message.type() == kInsertKeyFrame) {
            if (const auto state = weak_observations.lock()) ++state->key_requests;
        }
    });
    ASSERT_TRUE(server->start("127.0.0.1", 0));
    SdkHarness harness{};
    const auto parameters = std::make_shared<ThunderSdkParams>();
    parameters->ip_ = "127.0.0.1";
    parameters->port_ = server->listen_port();
    parameters->media_path_ = "/decoder-output-recovery";
    parameters->media_transport_ = SdkMediaTransport::kWebSocket;
    parameters->enable_video_ = true;
    ASSERT_TRUE(harness.sdk->Init(parameters, std::make_shared<WithheldOutputFactory>(observations)));
    harness.sdk->SetOnEncodedVideoFrameCallback([weak_observations](const std::shared_ptr<Message>&) {
        if (const auto state = weak_observations.lock()) ++state->received_frames;
    });
    harness.sdk->SetOnVideoFrameDecodedCallback([weak_observations](const std::shared_ptr<RawImage>&, const SdkCaptureMonitorInfo&) {
        if (const auto state = weak_observations.lock()) ++state->images;
    });
    harness.sdk->Start();
    ASSERT_TRUE(WaitFor([observations] {
        std::lock_guard lock(observations->session_mutex);
        return static_cast<bool>(observations->session);
    }));
    const auto send_frame = [observations](int frame_index, bool key, bool configured) {
        Message message{};
        message.set_type(kVideoFrame);
        auto& frame = *message.mutable_video_frame();
        frame.set_mon_name("recovery-monitor");
        frame.set_frame_index(frame_index);
        frame.set_frame_width(16);
        frame.set_frame_height(16);
        frame.set_type(kNetH264);
        frame.set_image_format(EImageFormat::kI420);
        frame.set_key(key);
        const std::string start_code{'\0', '\0', '\0', '\1'};
        frame.set_data(configured ? start_code + std::string{'\x67', '\x64', '\0', '\x28'} + start_code +
                                       std::string{'\x68', '\x01', '\x02'} + start_code + std::string{'\x65', '\x55'}
                                 : start_code + std::string{'\x41', '\x55'});
        std::lock_guard lock(observations->session_mutex);
        observations->session->async_send(message.SerializeAsString());
    };
    send_frame(1, true, true);
    ASSERT_TRUE(WaitFor([observations] { return observations->images == 1; }));
    const int baseline_requests = observations->key_requests.load();
    observations->withhold_output = true;
    const auto stalled_at = std::chrono::steady_clock::now();
    int frame_index{2};
    for (; frame_index <= 30 && observations->releases == 0; ++frame_index) {
        send_frame(frame_index, false, false);
        std::this_thread::sleep_for(17ms);
    }
    ASSERT_EQ(observations->releases, 1);
    EXPECT_LT(std::chrono::steady_clock::now() - stalled_at, 1s);
    ASSERT_TRUE(WaitFor([observations, baseline_requests] { return observations->key_requests == baseline_requests + 1; }));
    EXPECT_EQ(observations->creations, 1);
    send_frame(frame_index++, false, false);
    send_frame(frame_index++, true, false);
    const int expected_received = frame_index - 1;
    ASSERT_TRUE(WaitFor([observations, expected_received] { return observations->received_frames == expected_received; }));
    const auto drained = std::make_shared<std::promise<void>>();
    auto drain_completion = drained->get_future();
    harness.sdk->PostVideoTask([drained] { drained->set_value(); }, frame_index, "recovery-monitor");
    ASSERT_EQ(drain_completion.wait_for(3s), std::future_status::ready);
    EXPECT_EQ(observations->creations, 1);
    EXPECT_EQ(observations->images, 1);
    EXPECT_EQ(observations->key_requests, baseline_requests + 1);
    send_frame(frame_index, true, true);
    ASSERT_TRUE(WaitFor([observations] { return observations->images == 2; }));
    EXPECT_EQ(observations->creations, 2);
    harness.sdk->Exit();
    harness.sdk->Exit();
    EXPECT_EQ(observations->releases, 2);
    server->stop();
}

} // namespace
} // namespace px
