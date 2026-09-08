#pragma once

#include <memory>
#include <mutex>
#include <utility>

#include "px_client_sdk/sdk_voice_audio_port.h"
#include "px_voice_call/voice_audio_endpoint.h"

namespace px {

// Host-side adapter: consumers using this header link the existing px_voice_call device engine.
// SDK-only consumers may instead inject their own VoiceAudioPort without SDL/AAudio/WASAPI/APM dependencies.
class VoiceAudioEndpointPort final : public VoiceAudioPort {
  public:
    explicit VoiceAudioEndpointPort(VoiceAudioBackendConfig config = {}, VoiceAudioEndpoint::BackendFactory factory = {})
        : config_(std::move(config)), endpoint_(std::make_shared<VoiceAudioEndpoint>(std::move(factory))) {}

    ~VoiceAudioEndpointPort() override {
        Stop();
    }

    bool Start(EncodedCallback encoded, ErrorCallback error, std::string& reason) override {
        const auto callbacks = std::make_shared<const Callbacks>(Callbacks{std::move(encoded), std::move(error)});
        {
            std::lock_guard lock(mutex_);
            if (stopped_ || start_attempted_ || !callbacks->encoded || !callbacks->error) {
                reason = "audio_port_unavailable";
                return false;
            }
            start_attempted_ = true;
            callbacks_ = callbacks;
        }
        const auto weak = std::weak_ptr(callbacks);
        const bool started = endpoint_->Start(
            [weak](std::uint32_t sequence, std::uint64_t timestamp, const std::vector<std::uint8_t>& opus) {
                if (const auto active = weak.lock()) {
                    active->encoded({sequence, timestamp, opus});
                }
            },
            config_, reason,
            [weak](const std::string& failure) {
                if (const auto active = weak.lock()) {
                    active->error(failure);
                }
            });
        bool cancelled{};
        {
            std::lock_guard lock(mutex_);
            cancelled = stopped_;
        }
        if (!started || cancelled) {
            Stop();
            if (reason.empty()) {
                reason = cancelled ? "audio_port_cancelled" : "no_mic";
            }
            return false;
        }
        return true;
    }

    void Stop() override {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            callbacks_.reset();
        }
        endpoint_->Stop();
    }

    bool Receive(const VoiceTransportPacket& packet) override {
        return endpoint_->ReceiveOpus(packet.sequence, packet.capture_time_ms, packet.opus);
    }
    void SetMicrophoneMuted(bool muted) override {
        endpoint_->SetMicrophoneMuted(muted);
    }
    void SetSpeakerMuted(bool muted) override {
        endpoint_->SetSpeakerMuted(muted);
    }
    [[nodiscard]] VoiceAudioStats Stats() const override {
        return endpoint_->Stats();
    }

  private:
    struct Callbacks final {
        EncodedCallback encoded{};
        ErrorCallback error{};
    };

    const VoiceAudioBackendConfig config_{};
    const std::shared_ptr<VoiceAudioEndpoint> endpoint_{};
    std::mutex mutex_{};
    std::shared_ptr<const Callbacks> callbacks_{};
    bool start_attempted_{};
    bool stopped_{};
};

} // namespace px
