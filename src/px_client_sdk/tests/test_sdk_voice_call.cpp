#include "sdk_voice_call.h"
#include "px_voice_call/voice_audio_format.h"

#include <atomic>
#include <condition_variable>
#include <future>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

namespace px {
namespace {

using namespace std::chrono_literals;

class FakeVoicePort final : public VoiceAudioPort {
  public:
    bool Start(EncodedCallback encoded, ErrorCallback error, std::string& reason) override {
        {
            std::lock_guard lock(mutex);
            encoded_callback = std::move(encoded);
            error_callback = std::move(error);
        }
        ++starts;
        if (start_entered) {
            start_entered->set_value();
            if (release_start.wait_for(2s) != std::future_status::ready) {
                reason = "test_start_timeout";
                return false;
            }
        }
        if (fail_start || stops.load() != 0) {
            reason = "no_mic";
            return false;
        }
        return true;
    }
    void Stop() override {
        ++stops;
    }
    bool Receive(const VoiceTransportPacket&) override {
        ++received;
        return true;
    }
    void SetMicrophoneMuted(bool value) override {
        microphone_muted = value;
    }
    void SetSpeakerMuted(bool value) override {
        speaker_muted = value;
    }
    VoiceAudioStats Stats() const override {
        return {.decoded_packets = received.load()};
    }

    void Emit(VoiceTransportPacket packet = {0, 1, {1, 2, 3}}) {
        EncodedCallback callback{};
        {
            std::lock_guard lock(mutex);
            callback = encoded_callback;
        }
        if (callback) {
            callback(std::move(packet));
        }
    }
    void Fail() {
        ErrorCallback callback{};
        {
            std::lock_guard lock(mutex);
            callback = error_callback;
        }
        if (callback) {
            callback("device_lost");
        }
    }

    std::mutex mutex{};
    EncodedCallback encoded_callback{};
    ErrorCallback error_callback{};
    std::atomic_int starts{};
    std::atomic_int stops{};
    std::atomic_uint64_t received{};
    std::atomic_bool microphone_muted{};
    std::atomic_bool speaker_muted{};
    bool fail_start{};
    std::shared_ptr<std::promise<void>> start_entered{};
    std::shared_future<void> release_start{};
};

struct VoiceHarness final : std::enable_shared_from_this<VoiceHarness> {
    std::shared_ptr<VoiceCallController> controller{};
    mutable std::mutex mutex{};
    std::condition_variable changed{};
    std::vector<Message> controls{};
    std::vector<Message> audio{};
    std::vector<VoiceCallStatus> statuses{};
    std::vector<std::function<void()>> tasks{};
    std::vector<std::shared_ptr<FakeVoicePort>> ports{};
    std::shared_ptr<FakeVoicePort> next_port{};
    std::function<void(const Message&)> control_hook{};
    std::function<void()> audio_hook{};
    bool fail_control{};
    bool stop_from_status{};

    void Init(std::chrono::milliseconds timeout = 30s) {
        const auto weak = weak_from_this();
        VoiceCallDependencies dependencies{
            .send_control =
                [weak](std::shared_ptr<Message> message) {
                    const auto self = weak.lock();
                    if (!self) {
                        return false;
                    }
                    std::function<void(const Message&)> hook{};
                    bool fail{};
                    {
                        std::lock_guard lock(self->mutex);
                        self->controls.push_back(*message);
                        hook = self->control_hook;
                        fail = self->fail_control;
                    }
                    self->changed.notify_all();
                    if (hook) {
                        hook(*message);
                    }
                    return !fail;
                },
            .send_audio =
                [weak](std::shared_ptr<Message> message) {
                    const auto self = weak.lock();
                    if (!self) {
                        return false;
                    }
                    std::function<void()> hook{};
                    {
                        std::lock_guard lock(self->mutex);
                        self->audio.push_back(*message);
                        hook = self->audio_hook;
                    }
                    self->changed.notify_all();
                    if (hook) {
                        hook();
                    }
                    return true;
                },
            .create_audio = [weak]() -> std::shared_ptr<VoiceAudioPort> {
                const auto self = weak.lock();
                if (!self) {
                    return {};
                }
                std::lock_guard lock(self->mutex);
                auto port = std::exchange(self->next_port, {});
                if (!port) {
                    port = std::make_shared<FakeVoicePort>();
                }
                self->ports.push_back(port);
                return port;
            },
            .post_task =
                [weak](std::function<void()> task) {
                    const auto self = weak.lock();
                    if (!self) {
                        return false;
                    }
                    {
                        std::lock_guard lock(self->mutex);
                        self->tasks.push_back(std::move(task));
                    }
                    self->changed.notify_all();
                    return true;
                },
            .status_changed =
                [weak](const VoiceCallStatus& status) {
                    const auto self = weak.lock();
                    if (!self) {
                        return;
                    }
                    bool stop{};
                    {
                        std::lock_guard lock(self->mutex);
                        self->statuses.push_back(status);
                        stop = self->stop_from_status;
                    }
                    if (stop) {
                        self->controller->Stop(true, "status_callback");
                    }
                },
        };
        controller = VoiceCallController::Create({"device", "stream", timeout}, std::move(dependencies));
    }

    Message Request() const {
        std::lock_guard lock(mutex);
        for (auto it = controls.rbegin(); it != controls.rend(); ++it) {
            if (it->type() == kVoiceCallRequest && it->voice_call_request().connect()) {
                return *it;
            }
        }
        return {};
    }
    void Answer(const Message& request, bool accepted = true) {
        const auto& body = request.voice_call_request();
        controller->HandleMessage(std::make_shared<Message>(
            MakeVoiceCallResponseMessage("device", "stream", body.call_id(), body.request_id(), accepted, accepted ? "" : "rejected")));
    }
    void Drain() {
        std::vector<std::function<void()>> pending{};
        {
            std::lock_guard lock(mutex);
            pending.swap(tasks);
        }
        for (const auto& task : pending) {
            task();
        }
    }
    std::shared_ptr<FakeVoicePort> Port() const {
        std::lock_guard lock(mutex);
        return ports.empty() ? std::shared_ptr<FakeVoicePort>{} : ports.back();
    }
    bool WaitForAudio() {
        const auto self = shared_from_this();
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 2s, [self] { return !self->audio.empty(); });
    }
    bool WaitForPhase(VoiceCallPhase phase) {
        const auto self = shared_from_this();
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 2s, [self, phase] { return self->controller->Status().phase == phase; });
    }
};

std::shared_ptr<VoiceHarness> MakeVoice(std::chrono::milliseconds timeout = 30s) {
    const auto harness = std::make_shared<VoiceHarness>();
    harness->Init(timeout);
    harness->controller->SetCapabilities(true, false);
    return harness;
}

TEST(SdkVoiceCall, MediaFailureEndsOnlyVoiceAndBlocksRetryUntilTransportIsAvailable) {
    const auto harness = MakeVoice();
    ASSERT_TRUE(harness->controller->Start());
    const auto request = harness->Request();
    harness->Answer(request);
    ASSERT_EQ(harness->controller->Status().phase, VoiceCallPhase::kConnected);
    const auto port = harness->Port();
    harness->controller->SetTransportAvailable(false);
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kIdle);
    EXPECT_EQ(harness->controller->Status().reason, "media_unavailable");
    EXPECT_TRUE(harness->controller->Status().supported);
    EXPECT_GT(port->stops.load(), 0U);
    EXPECT_FALSE(harness->controller->Start());
    harness->Answer(request);
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kIdle);
    harness->controller->SetTransportAvailable(true);
    EXPECT_TRUE(harness->controller->Start());
    harness->controller->Close();
}

TEST(SdkVoiceCall, RequiresCapabilitiesAndDoesNotCaptureBeforeExactConsent) {
    const auto harness = std::make_shared<VoiceHarness>();
    harness->Init();
    EXPECT_FALSE(harness->controller->Start());
    EXPECT_TRUE(harness->controls.empty());
    harness->controller->SetCapabilities(true, false);
    ASSERT_TRUE(harness->controller->Start());
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kOutgoingPending);
    EXPECT_TRUE(harness->ports.empty());
    auto wrong = harness->Request();
    wrong.mutable_voice_call_request()->set_request_id(0);
    harness->Answer(wrong);
    EXPECT_TRUE(harness->ports.empty());
    harness->Answer(harness->Request());
    ASSERT_TRUE(harness->Port());
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kConnected);
    EXPECT_TRUE(harness->controller->SetMicrophoneMuted(true));
    EXPECT_TRUE(harness->controller->SetSpeakerMuted(true));
    EXPECT_TRUE(harness->Port()->microphone_muted.load());
    EXPECT_TRUE(harness->Port()->speaker_muted.load());
    harness->controller->Close();
}

TEST(SdkVoiceCall, AudioUsesSeparateSenderAndValidatesRouteCallPayloadAndReplay) {
    const auto harness = MakeVoice();
    ASSERT_TRUE(harness->controller->Start());
    const auto request = harness->Request();
    harness->Answer(request);
    const auto port = harness->Port();
    ASSERT_TRUE(port);
    port->Emit();
    ASSERT_TRUE(harness->WaitForAudio());
    const std::vector<std::uint8_t> opus{1, 2, 3};
    const auto call = request.voice_call_request().call_id();
    harness->controller->HandleMessage(std::make_shared<Message>(MakeVoiceAudioFrameMessage("wrong", "stream", call, 1, 1, opus)));
    harness->controller->HandleMessage(std::make_shared<Message>(MakeVoiceAudioFrameMessage("device", "stream", "old", 1, 1, opus)));
    harness->controller->HandleMessage(std::make_shared<Message>(MakeVoiceAudioFrameMessage("device", "stream", call, 1, 1, {})));
    EXPECT_EQ(port->received.load(), 0U);
    auto valid = std::make_shared<Message>(MakeVoiceAudioFrameMessage("device", "stream", call, 1, 1, opus));
    harness->controller->HandleMessage(valid);
    harness->controller->HandleMessage(valid);
    EXPECT_EQ(port->received.load(), 1U);
    harness->controller->Stop(true, "done");
    port->Emit({2, 2, opus});
    harness->controller->HandleMessage(valid);
    EXPECT_EQ(port->received.load(), 1U);
}

TEST(SdkVoiceCall, OldResponseHangupAndQueuedFailureCannotAffectRetriedCall) {
    const auto harness = MakeVoice();
    ASSERT_TRUE(harness->controller->Start());
    const auto old = harness->Request();
    harness->Answer(old);
    const auto port = harness->Port();
    port->Fail();
    harness->controller->Stop(true, "retry");
    ASSERT_TRUE(harness->controller->Start());
    const auto current = harness->Request();
    EXPECT_NE(old.voice_call_request().call_id(), current.voice_call_request().call_id());
    harness->Answer(old);
    harness->controller->HandleMessage(std::make_shared<Message>(
        MakeVoiceCallRequestMessage("device", "stream", old.voice_call_request().call_id(), old.voice_call_request().request_id(), false)));
    harness->Drain();
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kOutgoingPending);
    harness->Answer(current);
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kConnected);
    EXPECT_NE(harness->Port(), port);
}

TEST(SdkVoiceCall, TimeoutCancelsPendingRequestWithoutAllocatingAudio) {
    const auto harness = MakeVoice(20ms);
    ASSERT_TRUE(harness->controller->Start());
    const auto self = harness;
    {
        std::unique_lock lock(harness->mutex);
        ASSERT_TRUE(harness->changed.wait_for(lock, 2s, [self] { return self->controls.size() >= 2; }));
    }
    ASSERT_TRUE(harness->WaitForPhase(VoiceCallPhase::kIdle));
    harness->Drain();
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kIdle);
    EXPECT_TRUE(harness->ports.empty());
}

TEST(SdkVoiceCall, FailedSendAndAudioStartReturnIdleWithoutLeakingRun) {
    const auto harness = MakeVoice();
    harness->fail_control = true;
    EXPECT_FALSE(harness->controller->Start());
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kIdle);
    harness->fail_control = false;
    harness->next_port = std::make_shared<FakeVoicePort>();
    harness->next_port->fail_start = true;
    ASSERT_TRUE(harness->controller->Start());
    harness->Answer(harness->Request());
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kIdle);
    EXPECT_GE(harness->Port()->stops.load(), 1);
    EXPECT_EQ(harness->controller->Status().reason, "no_mic");
}

TEST(SdkVoiceCall, CancellationDuringAudioStartCannotStopNextRun) {
    const auto harness = MakeVoice();
    const auto blocked = std::make_shared<FakeVoicePort>();
    blocked->start_entered = std::make_shared<std::promise<void>>();
    auto entered = blocked->start_entered->get_future();
    const auto release = std::make_shared<std::promise<void>>();
    blocked->release_start = release->get_future().share();
    harness->next_port = blocked;
    ASSERT_TRUE(harness->controller->Start());
    const auto old = harness->Request();
    std::jthread accepting([harness, old] { harness->Answer(old); });
    ASSERT_EQ(entered.wait_for(2s), std::future_status::ready);
    harness->controller->Stop(true, "cancel_start");
    ASSERT_TRUE(harness->controller->Start());
    release->set_value();
    accepting.join();
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kOutgoingPending);
    blocked->Emit();
    harness->Answer(harness->Request());
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kConnected);
}

TEST(SdkVoiceCall, StopInsideSenderPreservesConnectThenCancelOrder) {
    const auto harness = MakeVoice();
    const auto weak = std::weak_ptr(harness);
    harness->control_hook = [weak](const Message& message) {
        if (message.type() == kVoiceCallRequest && message.voice_call_request().connect()) {
            if (const auto self = weak.lock()) {
                self->controller->Stop(true, "sender_callback");
            }
        }
    };
    EXPECT_FALSE(harness->controller->Start());
    ASSERT_EQ(harness->controls.size(), 2U);
    EXPECT_TRUE(harness->controls.front().voice_call_request().connect());
    EXPECT_FALSE(harness->controls.back().voice_call_request().connect());
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kIdle);
}

TEST(SdkVoiceCall, QueuedStatusSelfStopAndRepeatedCloseAreSafe) {
    for (int round{}; round < 10; ++round) {
        const auto harness = MakeVoice();
        harness->stop_from_status = true;
        ASSERT_TRUE(harness->controller->Start());
        harness->Drain();
        EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kIdle);
        harness->controller->Close();
        harness->controller->Close();
        EXPECT_FALSE(harness->controller->Start());
        harness->controller.reset();
        harness->Drain();
    }
}

TEST(SdkVoiceCall, StopFromAudioDeliveryCallbackDoesNotSelfJoin) {
    const auto harness = MakeVoice();
    const auto finished = std::make_shared<std::promise<void>>();
    auto future = finished->get_future();
    const auto weak = std::weak_ptr(harness);
    harness->audio_hook = [weak, finished] {
        if (const auto self = weak.lock()) {
            self->controller->Stop(true, "audio_send_callback");
            finished->set_value();
        }
    };
    ASSERT_TRUE(harness->controller->Start());
    harness->Answer(harness->Request());
    harness->Port()->Emit();
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(harness->controller->Status().phase, VoiceCallPhase::kIdle);
}

} // namespace
} // namespace px
