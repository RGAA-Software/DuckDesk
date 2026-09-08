#include "platform/voice_audio_endpoint_port.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <gtest/gtest.h>

namespace px {
namespace {

using namespace std::chrono_literals;

struct BackendProbe final {
    std::mutex mutex{};
    IVoiceAudioBackend::EventCallback event{};
    VoiceAudioBackendConfig config{};
    std::atomic_bool running{};
    std::atomic_uint starts{};
    std::atomic_uint stops{};
    bool fail_start{};
    std::shared_ptr<std::promise<void>> start_entered{};
    std::shared_future<void> release_start{};

    void EmitStopped() {
        IVoiceAudioBackend::EventCallback callback{};
        {
            std::lock_guard lock(mutex);
            callback = event;
        }
        if (callback) {
            callback(VoiceAudioBackendEvent::kStopped, "test_device_lost");
        }
    }
};

class InjectedBackend final : public IVoiceAudioBackend {
  public:
    explicit InjectedBackend(std::shared_ptr<BackendProbe> probe) : probe_(std::move(probe)) {}
    ~InjectedBackend() override {
        Stop();
    }

    bool Start(const VoiceAudioBackendConfig& config, CaptureCallback, PlayoutCallback, EventCallback event, std::string& reason) override {
        {
            std::lock_guard lock(probe_->mutex);
            probe_->config = config;
            probe_->event = std::move(event);
        }
        ++probe_->starts;
        if (probe_->start_entered) {
            probe_->start_entered->set_value();
            if (probe_->release_start.wait_for(2s) != std::future_status::ready) {
                reason = "test_start_timeout";
                return false;
            }
        }
        if (probe_->fail_start) {
            reason = "test_no_mic";
            return false;
        }
        probe_->running = true;
        return true;
    }
    void Stop() override {
        probe_->running = false;
        ++probe_->stops;
    }
    bool EnumerateDevices(VoiceAudioDeviceInventory&, std::string&) override {
        return true;
    }
    bool IsRunning() const override {
        return probe_->running.load();
    }
    VoiceAudioBackendInfo Info() const override {
        return {.backend = "injected"};
    }

  private:
    const std::shared_ptr<BackendProbe> probe_{};
};

std::shared_ptr<VoiceAudioEndpointPort> MakePort(const std::shared_ptr<BackendProbe>& probe, VoiceAudioBackendConfig config = {}) {
    return std::make_shared<VoiceAudioEndpointPort>(std::move(config), [probe] { return std::make_unique<InjectedBackend>(probe); });
}

TEST(SdkVoiceAudioPort, DeviceSelectionAndStartFailureRemainHostConcerns) {
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_start = true;
    VoiceAudioBackendConfig config{};
    config.capture_device_id = "test-capture";
    config.playout_device_id = "test-playout";
    const auto port = MakePort(probe, std::move(config));
    std::string reason{};
    EXPECT_FALSE(port->Start([](VoiceTransportPacket) {}, [](std::string) {}, reason));
    EXPECT_EQ(reason, "test_no_mic");
    EXPECT_EQ(probe->config.capture_device_id, "test-capture");
    EXPECT_EQ(probe->config.playout_device_id, "test-playout");
    port->Stop();
    port->Stop();
    EXPECT_FALSE(port->Start([](VoiceTransportPacket) {}, [](std::string) {}, reason));
    EXPECT_EQ(probe->starts, 1U);
    EXPECT_FALSE(probe->running);
}

TEST(SdkVoiceAudioPort, ClosingBeforeStartNeverOpensAnAudioDevice) {
    const auto probe = std::make_shared<BackendProbe>();
    const auto port = MakePort(probe);
    port->Stop();
    std::string reason{};
    EXPECT_FALSE(port->Start([](VoiceTransportPacket) {}, [](std::string) {}, reason));
    EXPECT_EQ(probe->starts, 0U);
}

TEST(SdkVoiceAudioPort, LateDeviceEventsAreDroppedAfterStopAndDestruction) {
    const auto probe = std::make_shared<BackendProbe>();
    const auto failures = std::make_shared<std::atomic_uint>();
    for (unsigned round{}; round != 5; ++round) {
        const auto port = MakePort(probe);
        std::string reason{};
        ASSERT_TRUE(port->Start([](VoiceTransportPacket) {}, [failures](std::string) { ++*failures; }, reason)) << reason;
        probe->EmitStopped();
        EXPECT_EQ(failures->load(), round + 1);
        port->Stop();
        probe->EmitStopped();
        EXPECT_EQ(failures->load(), round + 1);
        EXPECT_FALSE(port->Receive({0, 1, {1, 2, 3}}));
    }
    probe->EmitStopped();
    EXPECT_EQ(failures->load(), 5U);
}

TEST(SdkVoiceAudioPort, StopRacingDeviceInitializationCannotLeaveTheDeviceRunning) {
    const auto probe = std::make_shared<BackendProbe>();
    probe->start_entered = std::make_shared<std::promise<void>>();
    auto entered = probe->start_entered->get_future();
    const auto release = std::make_shared<std::promise<void>>();
    probe->release_start = release->get_future().share();
    const auto port = MakePort(probe);
    const auto result = std::make_shared<std::atomic_bool>();
    std::jthread start([port, result] {
        std::string reason{};
        *result = port->Start([](VoiceTransportPacket) {}, [](std::string) {}, reason);
    });
    EXPECT_EQ(entered.wait_for(1s), std::future_status::ready);
    std::jthread stop([port] { port->Stop(); });
    // Either ordering is legal; after both operations finish the per-call port is terminal.
    release->set_value();
    start.join();
    stop.join();
    EXPECT_FALSE(probe->running);
    std::string reason{};
    EXPECT_FALSE(port->Start([](VoiceTransportPacket) {}, [](std::string) {}, reason));
    EXPECT_EQ(probe->starts, 1U);
}

} // namespace
} // namespace px
