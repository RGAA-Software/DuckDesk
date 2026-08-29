#include "voice_call_state.h"
#include "voice_audio_endpoint.h"
#include "voice_audio_processing.h"
#include "voice_consent_decision_cache.h"
#include "voice_jitter_buffer.h"
#include "voice_packet_transport.h"

#include "px_render_panel_message.pb.h"

#include <gtest/gtest.h>

#include <SDL2/SDL.h>

#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#include <Psapi.h>
#endif

namespace px {
namespace {

class TestVoiceAudioBackend final : public IVoiceAudioBackend {
public:
    bool Start(
        const VoiceAudioBackendConfig&, CaptureCallback capture_callback,
        PlayoutCallback playout_callback, EventCallback event_callback,
        std::string*) override {
        capture_callback_ = std::move(capture_callback);
        playout_callback_ = std::move(playout_callback);
        event_callback_ = std::move(event_callback);
        running_ = true;
        return true;
    }

    void Stop() override { running_ = false; }

    bool EnumerateDevices(
        VoiceAudioDeviceInventory* inventory, std::string*) override {
        if (!inventory) {
            return false;
        }
        inventory->capture_devices.push_back({.name = "test capture", .is_default = true});
        inventory->playout_devices.push_back({.name = "test playout", .is_default = true});
        return true;
    }

    [[nodiscard]] bool IsRunning() const override { return running_; }
    [[nodiscard]] VoiceAudioBackendInfo Info() const override {
        return {.backend = "test", .capture_device = "test capture",
                .playout_device = "test playout"};
    }

    void Emit(VoiceAudioBackendEvent event, const std::string& reason = {}) {
        if (event_callback_) {
            event_callback_(event, reason);
        }
    }

    void FeedCapture(const int16_t* samples, size_t count) {
        if (running_ && capture_callback_) capture_callback_(samples, count);
    }

    std::vector<int16_t> PullPlayout(size_t count) {
        std::vector<int16_t> output(count, 0);
        if (running_ && playout_callback_) playout_callback_(output.data(), count);
        return output;
    }

private:
    bool running_ = false;
    CaptureCallback capture_callback_;
    PlayoutCallback playout_callback_;
    EventCallback event_callback_;
};

}  // namespace

TEST(VoiceCallStateTest, OutgoingAcceptanceRequiresExactRequest) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call-a", 7, 100));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kOutgoingPending);
    EXPECT_FALSE(state.ApplyResponse("call-a", 8, true));
    EXPECT_FALSE(state.ApplyResponse("call-b", 7, true));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kOutgoingPending);
    EXPECT_TRUE(state.ApplyResponse("call-a", 7, true));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kConnected);
}

TEST(VoiceCallStateTest, RejectedOutgoingRequestReturnsIdle) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call-a", 3, 100));
    EXPECT_TRUE(state.ApplyResponse("call-a", 3, false));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
    EXPECT_TRUE(state.CallId().empty());
}

TEST(VoiceCallStateTest, IncomingCallIsExclusiveAndDuplicateIsRecognized) {
    VoiceCallState state;
    EXPECT_EQ(state.BeginIncoming("call-a", 9, 100), IncomingVoiceCallResult::kPending);
    EXPECT_EQ(state.BeginIncoming("call-a", 9, 101), IncomingVoiceCallResult::kDuplicate);
    EXPECT_EQ(state.BeginIncoming("call-b", 10, 101), IncomingVoiceCallResult::kBusy);
    EXPECT_TRUE(state.AcceptIncoming("call-a", 9));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kConnected);
}

TEST(VoiceCallStateTest, InvalidIncomingRequestDoesNotReserveAudioResources) {
    VoiceCallState state;
    EXPECT_EQ(state.BeginIncoming("", 1, 100), IncomingVoiceCallResult::kInvalid);
    EXPECT_EQ(state.BeginIncoming("call", 0, 100), IncomingVoiceCallResult::kInvalid);
    EXPECT_EQ(state.BeginIncoming(
                  std::string(VoiceCallState::kMaxCallIdBytes + 1, 'x'), 1, 100),
              IncomingVoiceCallResult::kInvalid);
    EXPECT_FALSE(state.BeginOutgoing(
        std::string(VoiceCallState::kMaxCallIdBytes + 1, 'x'), 1, 100));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
}

TEST(VoiceCallStateTest, PendingRequestExpiresAtThirtySeconds) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 5'000));
    EXPECT_FALSE(state.Expire(34'999));
    EXPECT_TRUE(state.Expire(35'000));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
}

TEST(VoiceCallStateTest, MediaRequiresConnectedMatchingCall) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 0));
    EXPECT_FALSE(state.AcceptMedia("call", 1));
    ASSERT_TRUE(state.ApplyResponse("call", 1, true));
    EXPECT_FALSE(state.AcceptMedia("forged", 1));
    EXPECT_TRUE(state.AcceptMedia("call", 1));
}

TEST(VoiceCallStateTest, ReplayWindowAllowsReorderingButRejectsDuplicatesAndOldMedia) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 0));
    ASSERT_TRUE(state.ApplyResponse("call", 1, true));
    EXPECT_TRUE(state.AcceptMedia("call", 100));
    EXPECT_FALSE(state.AcceptMedia("call", 100));
    EXPECT_TRUE(state.AcceptMedia("call", 102));
    EXPECT_TRUE(state.AcceptMedia("call", 101));
    EXPECT_FALSE(state.AcceptMedia("call", 101));
    EXPECT_TRUE(state.AcceptMedia("call", 200));
    EXPECT_FALSE(state.AcceptMedia("call", 100));
}

TEST(VoiceCallStateTest, SequenceComparisonHandlesUint32Wrap) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 0));
    ASSERT_TRUE(state.ApplyResponse("call", 1, true));
    EXPECT_TRUE(state.AcceptMedia("call", 0xfffffffeu));
    EXPECT_TRUE(state.AcceptMedia("call", 1u));
}

TEST(VoiceCallStateTest, WrongCallCannotHangUpActiveCall) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 0));
    ASSERT_TRUE(state.ApplyResponse("call", 1, true));
    EXPECT_FALSE(state.HangUp("old-call"));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kConnected);
    EXPECT_TRUE(state.HangUp("call"));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
}

TEST(VoiceCallStateTest, CleanupIsIdempotent) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginIncoming("call", 1, 0) == IncomingVoiceCallResult::kPending);
    state.Reset();
    state.Reset();
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
    EXPECT_FALSE(state.HangUp("call"));
}

TEST(VoiceCallStateTest, LogCorrelationTokenDoesNotExposeCallId) {
    const auto first = VoiceCallLogId("sensitive-call-id");
    EXPECT_EQ(first.size(), 8u);
    EXPECT_EQ(first, VoiceCallLogId("sensitive-call-id"));
    EXPECT_NE(first, VoiceCallLogId("another-call-id"));
    EXPECT_EQ(first.find("sensitive"), std::string::npos);
}

TEST(VoiceAudioEndpointTest, CaptureEncodeDecodeAndPlayoutRunWithDummyAudioDevice) {
    ASSERT_EQ(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1), 0);

    VoiceAudioEndpoint endpoint([] { return CreateSdlVoiceAudioBackend(); });
    std::string error;
    ASSERT_TRUE(endpoint.Start(
        [&endpoint](uint32_t sequence, uint64_t capture_time_ms,
                    const std::vector<uint8_t>& opus) {
            endpoint.ReceiveOpus(
                sequence, capture_time_ms, std::span<const uint8_t>(opus));
        }, &error)) << error;

    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    const auto stats = endpoint.Stats();
    endpoint.Stop();

    EXPECT_GT(stats.encoded_packets, 0u);
    EXPECT_GT(stats.decoded_packets, 0u);
    EXPECT_GT(stats.apm_capture_frames, 0u);
    EXPECT_GT(stats.apm_render_frames, 0u);
}

TEST(VoiceAudioEndpointTest, MuteControlsKeepTransportAliveAndRecover) {
    ASSERT_EQ(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1), 0);

    VoiceAudioEndpoint endpoint([] { return CreateSdlVoiceAudioBackend(); });
    std::string error;
    ASSERT_TRUE(endpoint.Start(
        [&endpoint](uint32_t sequence, uint64_t capture_time_ms,
                    const std::vector<uint8_t>& opus) {
            endpoint.ReceiveOpus(
                sequence, capture_time_ms, std::span<const uint8_t>(opus));
        }, &error)) << error;

    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    const auto before_mute = endpoint.Stats();
    endpoint.SetMicrophoneMuted(true);
    endpoint.SetSpeakerMuted(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    const auto while_muted = endpoint.Stats();
    endpoint.SetMicrophoneMuted(false);
    endpoint.SetSpeakerMuted(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    const auto after_unmute = endpoint.Stats();
    endpoint.Stop();

    EXPECT_GT(while_muted.encoded_packets, before_mute.encoded_packets);
    EXPECT_GT(while_muted.decoded_packets, before_mute.decoded_packets);
    EXPECT_GT(after_unmute.encoded_packets, while_muted.encoded_packets);
    EXPECT_GT(after_unmute.decoded_packets, while_muted.decoded_packets);
    EXPECT_FALSE(endpoint.IsRunning());
}

TEST(VoiceAudioEndpointTest, ConcurrentStopIsIdempotent) {
    ASSERT_EQ(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1), 0);

    VoiceAudioEndpoint endpoint([] { return CreateSdlVoiceAudioBackend(); });
    std::string error;
    ASSERT_TRUE(endpoint.Start(
        [&endpoint](uint32_t sequence, uint64_t capture_time_ms,
                    const std::vector<uint8_t>& opus) {
            endpoint.ReceiveOpus(
                sequence, capture_time_ms, std::span<const uint8_t>(opus));
        }, &error)) << error;
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    std::thread first([&endpoint] { endpoint.Stop(); });
    std::thread second([&endpoint] { endpoint.Stop(); });
    first.join();
    second.join();
    endpoint.Stop();

    EXPECT_FALSE(endpoint.IsRunning());
}

TEST(VoiceAudioBackendTest, SdlDummyEnumeratesDefaultDevices) {
    ASSERT_EQ(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1), 0);
    auto backend = CreateSdlVoiceAudioBackend();
    ASSERT_NE(backend, nullptr);
    VoiceAudioDeviceInventory inventory;
    std::string error;
    ASSERT_TRUE(backend->EnumerateDevices(&inventory, &error)) << error;
    ASSERT_FALSE(inventory.capture_devices.empty());
    ASSERT_FALSE(inventory.playout_devices.empty());
    EXPECT_TRUE(inventory.capture_devices.front().is_default);
    EXPECT_TRUE(inventory.playout_devices.front().is_default);
}

TEST(VoiceAudioEndpointTest, DeviceEventsRebuildApmAndSignalFatalOnce) {
    TestVoiceAudioBackend* raw_backend = nullptr;
    VoiceAudioEndpoint endpoint([&raw_backend] {
        auto backend = std::make_unique<TestVoiceAudioBackend>();
        raw_backend = backend.get();
        return backend;
    });
    std::atomic_uint32_t fatal_count = 0;
    std::string fatal_reason;
    std::string error;
    ASSERT_TRUE(endpoint.Start(
        [](uint32_t, uint64_t, const std::vector<uint8_t>&) {}, &error,
        [&fatal_count, &fatal_reason](const std::string& reason) {
            ++fatal_count;
            fatal_reason = reason;
        })) << error;
    ASSERT_NE(raw_backend, nullptr);

    raw_backend->Emit(VoiceAudioBackendEvent::kRerouted, "default_changed");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(endpoint.Stats().device_rebuilds, 1u);
    raw_backend->Emit(VoiceAudioBackendEvent::kStopped, "first");
    raw_backend->Emit(VoiceAudioBackendEvent::kStopped, "duplicate");
    EXPECT_EQ(fatal_count.load(), 1u);
    EXPECT_EQ(fatal_reason, "device_lost");
    endpoint.Stop();
}

TEST(VoiceAudioEndpointTest, WebRtcPcmUsesEndpointPlayoutAndAecReference) {
    TestVoiceAudioBackend* raw_backend = nullptr;
    VoiceAudioEndpoint endpoint([&raw_backend] {
        auto backend = std::make_unique<TestVoiceAudioBackend>();
        raw_backend = backend.get();
        return backend;
    });
    std::string error;
    ASSERT_TRUE(endpoint.Start(
        [](uint32_t, uint64_t, const std::vector<uint8_t>&) {}, &error)) << error;
    ASSERT_NE(raw_backend, nullptr);

    std::array<int16_t, 960> stereo{};
    for (size_t frame = 0; frame < 480; ++frame) {
        stereo[frame * 2] = 1'000;
        stereo[frame * 2 + 1] = 3'000;
    }
    ASSERT_TRUE(endpoint.ReceivePcm(std::span<const int16_t>(stereo), 48'000, 2));
    EXPECT_EQ(endpoint.Stats().received_pcm_samples, 480u);
    const auto played = raw_backend->PullPlayout(480);
    ASSERT_EQ(played.size(), 480u);
    EXPECT_TRUE(std::all_of(played.begin(), played.end(), [](int16_t value) {
        return value == 2'000;
    }));

    std::array<int16_t, 480> capture{};
    raw_backend->FeedCapture(capture.data(), capture.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_GT(endpoint.Stats().apm_render_frames, 0u);

    endpoint.SetSpeakerMuted(true);
    EXPECT_TRUE(endpoint.ReceivePcm(std::span<const int16_t>(stereo), 48'000, 2));
    EXPECT_EQ(endpoint.Stats().received_pcm_samples, 480u);
    const auto muted = raw_backend->PullPlayout(480);
    EXPECT_TRUE(std::all_of(muted.begin(), muted.end(), [](int16_t value) {
        return value == 0;
    }));
    EXPECT_FALSE(endpoint.ReceivePcm(std::span<const int16_t>(stereo), 44'100, 2));
    EXPECT_EQ(endpoint.Stats().received_pcm_samples, 480u);
    endpoint.Stop();
}

TEST(VoiceAudioEndpointTest, ConfigurableLongRunningStability) {
    const char* duration_text = std::getenv("PX_TEST_VOICE_LONG_DURATION_SECONDS");
    if (!duration_text || *duration_text == '\0') {
        GTEST_SKIP() << "Set PX_TEST_VOICE_LONG_DURATION_SECONDS to run long stability";
    }
    char* end = nullptr;
    const long duration_seconds = std::strtol(duration_text, &end, 10);
    ASSERT_NE(end, duration_text);
    ASSERT_EQ(*end, '\0');
    ASSERT_GE(duration_seconds, 2);
    ASSERT_LE(duration_seconds, 8 * 60 * 60);

    const bool use_wasapi = [] {
        const char* enabled = std::getenv("PX_TEST_VOICE_WASAPI");
        return enabled && std::string_view(enabled) == "1";
    }();
    if (!use_wasapi) {
        ASSERT_EQ(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1), 0);
    }

    VoiceAudioEndpoint::BackendFactory backend_factory;
    if (!use_wasapi) {
        backend_factory = [] { return CreateSdlVoiceAudioBackend(); };
    }
    VoiceAudioEndpoint endpoint(std::move(backend_factory));
    std::string error;
    ASSERT_TRUE(endpoint.Start(
        [&endpoint](uint32_t sequence, uint64_t capture_time_ms,
                    const std::vector<uint8_t>& opus) {
            endpoint.ReceiveOpus(
                sequence, capture_time_ms, std::span<const uint8_t>(opus));
        }, &error)) << error;

    // Exclude one-time DLL, codec and audio subsystem initialization from the
    // leak baseline. Long-term growth is measured while the call is active.
    std::this_thread::sleep_for(std::chrono::seconds(2));
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX memory_before{};
    memory_before.cb = sizeof(memory_before);
    ASSERT_TRUE(GetProcessMemoryInfo(
        GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory_before),
        sizeof(memory_before)));
    DWORD handles_before = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
#endif

    uint64_t last_encoded = 0;
    long stagnant_seconds = 0;
    bool muted = false;
    for (long second = 1; second <= duration_seconds; ++second) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (second % 300 == 0) {
            muted = true;
            endpoint.SetMicrophoneMuted(true);
            endpoint.SetSpeakerMuted(true);
        }
        else if (muted) {
            muted = false;
            endpoint.SetMicrophoneMuted(false);
            endpoint.SetSpeakerMuted(false);
        }
        const auto stats = endpoint.Stats();
        EXPECT_LE(stats.jitter_queued_packets, 10u);
        if (stats.encoded_packets > last_encoded) {
            stagnant_seconds = 0;
        }
        else {
            ++stagnant_seconds;
        }
        EXPECT_LT(stagnant_seconds, 10) << "audio transport stopped progressing";
        last_encoded = stats.encoded_packets;
    }
    const auto final_stats = endpoint.Stats();
    EXPECT_GT(final_stats.encoded_packets,
              static_cast<uint64_t>(duration_seconds) * 35u);
    if (use_wasapi) {
        EXPECT_GT(final_stats.decoded_packets,
                  static_cast<uint64_t>(duration_seconds) * 30u);
    }
    else {
        // SDL's dummy playback clock is intentionally not real-time on every
        // platform. Progress and bounded jitter are the deterministic gates;
        // real-time playout throughput is covered by the WASAPI hardware run.
        EXPECT_GT(final_stats.decoded_packets, 0u);
    }
    EXPECT_LE(final_stats.jitter_peak_packets, 10u);
    RecordProperty("encoded_packets", final_stats.encoded_packets);
    RecordProperty("decoded_packets", final_stats.decoded_packets);
    RecordProperty("jitter_peak_packets", final_stats.jitter_peak_packets);
    RecordProperty("playout_underruns", final_stats.playout_underruns);
    RecordProperty("plc_packets", final_stats.plc_packets);
    RecordProperty("capture_samples_dropped", final_stats.capture_samples_dropped);
    RecordProperty("playout_samples_dropped", final_stats.playout_samples_dropped);
    RecordProperty("apm_capture_failures", final_stats.apm_capture_failures);
    RecordProperty("apm_render_failures", final_stats.apm_render_failures);
    RecordProperty("device_rebuilds", final_stats.device_rebuilds);
    RecordProperty("device_failures", final_stats.device_failures);

#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX memory_after{};
    memory_after.cb = sizeof(memory_after);
    ASSERT_TRUE(GetProcessMemoryInfo(
        GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory_after),
        sizeof(memory_after)));
    DWORD handles_after = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    EXPECT_LE(memory_after.PrivateUsage,
              memory_before.PrivateUsage + 64ull * 1024ull * 1024ull);
    EXPECT_LE(handles_after, handles_before + 32u);
    RecordProperty("private_bytes_before", memory_before.PrivateUsage);
    RecordProperty("private_bytes_after", memory_after.PrivateUsage);
    RecordProperty("handles_before", handles_before);
    RecordProperty("handles_after", handles_after);
#endif
    endpoint.Stop();
}

#if defined(_WIN32)
TEST(VoiceAudioBackendTest, WasapiCommunicationDuplexHardwareSmoke) {
    const char* enabled = std::getenv("PX_TEST_VOICE_WASAPI");
    if (!enabled || std::string_view(enabled) != "1") {
        GTEST_SKIP() << "Set PX_TEST_VOICE_WASAPI=1 on an interactive Windows host";
    }

    auto backend = CreateWasapiVoiceAudioBackend();
    ASSERT_NE(backend, nullptr);
    VoiceAudioDeviceInventory inventory;
    std::string enumeration_error;
    ASSERT_TRUE(backend->EnumerateDevices(&inventory, &enumeration_error))
        << enumeration_error;
    ASSERT_GT(inventory.capture_devices.size(), 1u);
    ASSERT_GT(inventory.playout_devices.size(), 1u);
    std::atomic_uint64_t captured_samples = 0;
    std::atomic_uint64_t rendered_samples = 0;
    std::string error;
    VoiceAudioBackendConfig config;
    if (const char* capture_id = std::getenv("PX_TEST_VOICE_CAPTURE_DEVICE_ID")) {
        config.capture_device_id = capture_id;
    }
    if (const char* playout_id = std::getenv("PX_TEST_VOICE_PLAYOUT_DEVICE_ID")) {
        config.playout_device_id = playout_id;
    }
    ASSERT_TRUE(backend->Start(
        config,
        [&captured_samples](const int16_t*, size_t count) {
            captured_samples.fetch_add(count, std::memory_order_relaxed);
        },
        [&rendered_samples](int16_t* output, size_t count) {
            std::fill(output, output + count, 0);
            rendered_samples.fetch_add(count, std::memory_order_relaxed);
        },
        [](VoiceAudioBackendEvent, const std::string&) {},
        &error)) << error;
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const auto info = backend->Info();
    backend->Stop();

    EXPECT_GT(captured_samples.load(), 0u);
    EXPECT_GT(rendered_samples.load(), 0u);
    EXPECT_NE(info.backend.find("WASAPI"), std::string::npos);
    EXPECT_FALSE(info.capture_device.empty());
    EXPECT_FALSE(info.playout_device.empty());
}
#endif

TEST(VoiceAudioProcessingTest, ConfiguresAndProcessesTenMillisecondFrames) {
    VoiceAudioProcessing processing;
    VoiceAudioProcessingConfig config{
        .echo_cancellation = true,
        .noise_suppression = true,
        .automatic_gain_control = true,
    };
    ASSERT_TRUE(processing.Initialize(config));

    std::array<int16_t, VoiceAudioProcessing::kFrameSamples> render{};
    std::array<int16_t, VoiceAudioProcessing::kFrameSamples> capture{};
    for (size_t i = 0; i < capture.size(); ++i) {
        capture[i] = static_cast<int16_t>((i % 64) * 40 - 1'280);
    }
    EXPECT_TRUE(processing.ProcessRender(render.data(), render.size()));
    EXPECT_TRUE(processing.ProcessCapture(capture.data(), capture.size()));
    EXPECT_FALSE(processing.ProcessCapture(capture.data(), capture.size() - 1));

    const auto stats = processing.Stats();
    EXPECT_EQ(stats.render_frames, 1u);
    EXPECT_EQ(stats.capture_frames, 1u);
    EXPECT_EQ(stats.capture_failures, 1u);
    EXPECT_TRUE(processing.IsInitialized());
    EXPECT_TRUE(processing.Config().echo_cancellation);
}

TEST(VoiceJitterBufferTest, ReordersPacketsAfterSixtyMillisecondPrefill) {
    VoiceJitterBuffer jitter;
    EXPECT_EQ(jitter.Push({100, 1'000, 10, {1}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Push({102, 1'040, 12, {3}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Pop(50).kind, VoiceJitterPopKind::kNotReady);
    EXPECT_EQ(jitter.Push({101, 1'020, 15, {2}}), VoiceJitterPushResult::kAccepted);

    auto first = jitter.Pop(15);
    auto second = jitter.Pop(35);
    auto third = jitter.Pop(55);
    ASSERT_EQ(first.kind, VoiceJitterPopKind::kPacket);
    ASSERT_EQ(second.kind, VoiceJitterPopKind::kPacket);
    ASSERT_EQ(third.kind, VoiceJitterPopKind::kPacket);
    EXPECT_EQ(first.packet->sequence, 100u);
    EXPECT_EQ(second.packet->sequence, 101u);
    EXPECT_EQ(third.packet->sequence, 102u);
}

TEST(VoiceJitterBufferTest, ReportsLossAndRejectsLateOrDuplicatePackets) {
    VoiceJitterBuffer jitter(1, 4);
    EXPECT_EQ(jitter.Push({7, 100, 10, {1}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Push({7, 100, 11, {1}}), VoiceJitterPushResult::kDuplicate);
    EXPECT_EQ(jitter.Pop(10).packet->sequence, 7u);
    EXPECT_EQ(jitter.Push({9, 140, 30, {3}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Pop(30).kind, VoiceJitterPopKind::kMissing);
    EXPECT_EQ(jitter.Push({8, 120, 31, {2}}), VoiceJitterPushResult::kLate);
    EXPECT_EQ(jitter.Pop(50).packet->sequence, 9u);
    EXPECT_EQ(jitter.Stats().missing, 1u);
}

TEST(VoiceJitterBufferTest, SequenceOrderingHandlesUint32Wrap) {
    VoiceJitterBuffer jitter(3, 5);
    EXPECT_EQ(jitter.Push({0xffffffffu, 20, 2, {2}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Push({0u, 40, 3, {3}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Push({0xfffffffeu, 0, 1, {1}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Pop(3).packet->sequence, 0xfffffffeu);
    EXPECT_EQ(jitter.Pop(23).packet->sequence, 0xffffffffu);
    EXPECT_EQ(jitter.Pop(43).packet->sequence, 0u);
}

TEST(VoiceJitterBufferTest, BoundsPayloadAndQueueCapacity) {
    VoiceJitterBuffer jitter(2, 3);
    EXPECT_EQ(jitter.Push({1, 0, 0, {}}), VoiceJitterPushResult::kInvalid);
    EXPECT_EQ(jitter.Push({1, 0, 0,
        std::vector<uint8_t>(VoiceJitterBuffer::kMaxOpusPacketBytes + 1)}),
        VoiceJitterPushResult::kInvalid);
    EXPECT_EQ(jitter.Push({1, 0, 1, {1}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Push({2, 0, 2, {2}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Push({3, 0, 3, {3}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Push({4, 0, 4, {4}}), VoiceJitterPushResult::kAccepted);
    EXPECT_EQ(jitter.Stats().queued, 3u);
    EXPECT_EQ(jitter.Stats().overflow_drops, 1u);
}

TEST(VoicePacketTransportTest, KeepsLatestSpeechUnderBlockedNetwork) {
    VoicePacketTransport transport;
    std::mutex mutex;
    std::condition_variable cv;
    bool first_entered = false;
    bool release_first = false;
    std::vector<uint32_t> sent;
    ASSERT_TRUE(transport.Start([&](const VoiceTransportPacket& packet) {
        std::unique_lock lock(mutex);
        sent.push_back(packet.sequence);
        if (packet.sequence == 0) {
            first_entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return release_first; });
        }
        cv.notify_all();
    }));

    ASSERT_TRUE(transport.Enqueue({.sequence = 0, .opus = {1}}));
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&] {
            return first_entered;
        }));
    }
    for (uint32_t sequence = 1; sequence <= 20; ++sequence) {
        ASSERT_TRUE(transport.Enqueue({.sequence = sequence, .opus = {1}}));
    }
    EXPECT_LE(transport.Stats().queued, VoicePacketTransport::kMaxQueuedPackets);
    {
        std::scoped_lock lock(mutex);
        release_first = true;
    }
    cv.notify_all();
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&] {
            return sent.size() >= 2;
        }));
    }
    transport.Stop();

    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent.front(), 0u);
    EXPECT_EQ(sent.back(), 20u);
    const auto stats = transport.Stats();
    EXPECT_EQ(stats.enqueued, 21u);
    EXPECT_EQ(stats.sent, 2u);
    EXPECT_EQ(stats.congestion_drops, 19u);
    EXPECT_LE(stats.peak_queued, VoicePacketTransport::kMaxQueuedPackets);
}

TEST(VoicePacketTransportTest, StopFromDeliveryCallbackDoesNotSelfJoin) {
    auto transport = std::make_shared<VoicePacketTransport>();
    const auto weak_transport = std::weak_ptr<VoicePacketTransport>(transport);
    auto stopped = std::make_shared<std::promise<void>>();
    auto stopped_future = stopped->get_future();
    ASSERT_TRUE(transport->Start(
        [weak_transport, stopped](const VoiceTransportPacket&) {
            if (const auto active = weak_transport.lock()) {
                active->Stop();
            }
            stopped->set_value();
        }));
    ASSERT_TRUE(transport->Enqueue({.sequence = 1, .opus = {1}}));
    EXPECT_EQ(stopped_future.wait_for(std::chrono::seconds(1)),
              std::future_status::ready);
    EXPECT_FALSE(transport->Enqueue({.sequence = 2, .opus = {1}}));
}

TEST(VoicePacketTransportTest, RepeatedStartStopIsStableForTenRounds) {
    VoicePacketTransport transport;
    for (uint32_t round = 0; round < 10; ++round) {
        auto delivered = std::make_shared<std::promise<uint32_t>>();
        auto delivered_future = delivered->get_future();
        ASSERT_TRUE(transport.Start(
            [delivered](const VoiceTransportPacket& packet) {
                delivered->set_value(packet.sequence);
            }));
        ASSERT_TRUE(transport.Enqueue({.sequence = round, .opus = {1}}));
        ASSERT_EQ(delivered_future.wait_for(std::chrono::seconds(1)),
                  std::future_status::ready);
        EXPECT_EQ(delivered_future.get(), round);
        transport.Stop();
        EXPECT_FALSE(transport.Enqueue({.sequence = round, .opus = {1}}));
    }
}

TEST(VoiceConsentDecisionCacheTest, LookupRequiresExactCallAndRequest) {
    VoiceConsentDecisionCache cache(100, 4);
    cache.Put("call-a", 7, true, "accepted", 1'000);

    const auto decision = cache.Find("call-a", 7, 1'050);
    ASSERT_TRUE(decision.has_value());
    EXPECT_TRUE(decision->accepted);
    EXPECT_EQ(decision->reason, "accepted");
    EXPECT_FALSE(cache.Find("call-a", 8, 1'050).has_value());
    EXPECT_FALSE(cache.Find("call-b", 7, 1'050).has_value());
}

TEST(VoiceConsentDecisionCacheTest, ExistingDecisionCanBeClosedAsStale) {
    VoiceConsentDecisionCache cache(100, 4);
    cache.Put("call", 1, true, "accepted", 1'000);
    cache.Put("call", 1, false, "stale_request", 1'010);

    const auto decision = cache.Find("call", 1, 1'050);
    ASSERT_TRUE(decision.has_value());
    EXPECT_FALSE(decision->accepted);
    EXPECT_EQ(decision->reason, "stale_request");
    EXPECT_EQ(cache.Size(1'050), 1u);
}

TEST(VoiceConsentDecisionCacheTest, DecisionExpiresAtConfiguredBoundary) {
    VoiceConsentDecisionCache cache(100, 4);
    cache.Put("call", 1, false, "rejected", 1'000);

    EXPECT_TRUE(cache.Find("call", 1, 1'099).has_value());
    EXPECT_FALSE(cache.Find("call", 1, 1'100).has_value());
    EXPECT_EQ(cache.Size(1'100), 0u);
}

TEST(VoiceConsentDecisionCacheTest, OldestDecisionIsEvictedAtCapacity) {
    VoiceConsentDecisionCache cache(1'000, 2);
    cache.Put("call-a", 1, false, "rejected", 1'000);
    cache.Put("call-b", 2, false, "rejected", 1'001);
    cache.Put("call-c", 3, true, "accepted", 1'002);

    EXPECT_FALSE(cache.Find("call-a", 1, 1'003).has_value());
    EXPECT_TRUE(cache.Find("call-b", 2, 1'003).has_value());
    EXPECT_TRUE(cache.Find("call-c", 3, 1'003).has_value());
}

TEST(VoiceConsentDecisionCacheTest, InvalidIdentityIsNotCached) {
    VoiceConsentDecisionCache cache;
    cache.Put("", 1, true, "accepted", 1'000);
    cache.Put("call", 0, true, "accepted", 1'000);
    EXPECT_EQ(cache.Size(1'000), 0u);
}

TEST(VoiceConsentPanelProtocolTest, RequestRoundTripsCorrelationAndDeadline) {
    pxrp::RpMessage message;
    message.set_type(pxrp::RpMessageType::kRpVoiceCallConsentRequest);
    auto* request = message.mutable_voice_call_consent_request();
    request->set_visitor_device_id("visitor-1");
    request->set_stream_id("stream-42");
    request->set_call_id("call-1");
    request->set_request_id(99);
    request->set_expires_at_unix_ms(123'456);
    request->set_protocol_version(1);

    pxrp::RpMessage decoded;
    ASSERT_TRUE(decoded.ParseFromString(message.SerializeAsString()));
    ASSERT_EQ(decoded.type(), pxrp::RpMessageType::kRpVoiceCallConsentRequest);
    ASSERT_TRUE(decoded.has_voice_call_consent_request());
    EXPECT_EQ(decoded.voice_call_consent_request().visitor_device_id(), "visitor-1");
    EXPECT_EQ(decoded.voice_call_consent_request().stream_id(), "stream-42");
    EXPECT_EQ(decoded.voice_call_consent_request().call_id(), "call-1");
    EXPECT_EQ(decoded.voice_call_consent_request().request_id(), 99u);
    EXPECT_EQ(decoded.voice_call_consent_request().expires_at_unix_ms(), 123'456u);
    EXPECT_EQ(decoded.voice_call_consent_request().protocol_version(), 1u);
}

TEST(VoiceConsentPanelProtocolTest, CancelRoundTripsExactCorrelation) {
    pxrp::RpMessage message;
    message.set_type(pxrp::RpMessageType::kRpVoiceCallConsentCancel);
    auto* cancel = message.mutable_voice_call_consent_cancel();
    cancel->set_stream_id("stream-42");
    cancel->set_call_id("call-1");
    cancel->set_request_id(99);
    cancel->set_reason("remote_cancelled");

    pxrp::RpMessage decoded;
    ASSERT_TRUE(decoded.ParseFromString(message.SerializeAsString()));
    ASSERT_EQ(decoded.type(), pxrp::RpMessageType::kRpVoiceCallConsentCancel);
    ASSERT_TRUE(decoded.has_voice_call_consent_cancel());
    EXPECT_EQ(decoded.voice_call_consent_cancel().stream_id(), "stream-42");
    EXPECT_EQ(decoded.voice_call_consent_cancel().call_id(), "call-1");
    EXPECT_EQ(decoded.voice_call_consent_cancel().request_id(), 99u);
    EXPECT_EQ(decoded.voice_call_consent_cancel().reason(), "remote_cancelled");
}

TEST(VoiceConsentPanelProtocolTest, DecisionRoundTripsExactCorrelation) {
    pxrp::RpMessage message;
    message.set_type(pxrp::RpMessageType::kRpVoiceCallConsentDecision);
    auto* decision = message.mutable_voice_call_consent_decision();
    decision->set_stream_id("stream-42");
    decision->set_call_id("call-1");
    decision->set_request_id(99);
    decision->set_accepted(false);
    decision->set_reason("rejected");

    pxrp::RpMessage decoded;
    ASSERT_TRUE(decoded.ParseFromString(message.SerializeAsString()));
    ASSERT_EQ(decoded.type(), pxrp::RpMessageType::kRpVoiceCallConsentDecision);
    ASSERT_TRUE(decoded.has_voice_call_consent_decision());
    EXPECT_EQ(decoded.voice_call_consent_decision().stream_id(), "stream-42");
    EXPECT_EQ(decoded.voice_call_consent_decision().call_id(), "call-1");
    EXPECT_EQ(decoded.voice_call_consent_decision().request_id(), 99u);
    EXPECT_FALSE(decoded.voice_call_consent_decision().accepted());
    EXPECT_EQ(decoded.voice_call_consent_decision().reason(), "rejected");
}

}  // namespace px
