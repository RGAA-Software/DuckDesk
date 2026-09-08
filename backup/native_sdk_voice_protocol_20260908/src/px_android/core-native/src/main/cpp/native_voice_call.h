#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "px_voice_call/voice_call_state.h"
#include "px_voice_call/voice_packet_transport.h"

namespace px {
class Data;
class Message;
class VoiceAudioEndpoint;
} // namespace px

namespace pixels::android {

struct NativeVoiceCallStatus final {
    std::int32_t phase{};
    bool microphone_muted{};
    bool speaker_muted{};
    bool requires_headset{true};
    std::string reason{};
};

class NativeVoiceCall final : public std::enable_shared_from_this<NativeVoiceCall> {
  public:
    using SendMessage = std::function<bool(std::shared_ptr<px::Data>)>;
    using PostTask = std::function<bool(std::function<void()>)>;
    using StatusCallback = std::function<void(const NativeVoiceCallStatus&)>;

    static std::shared_ptr<NativeVoiceCall> Create(SendMessage send_message, PostTask post_task, StatusCallback status_callback);

    NativeVoiceCall(SendMessage send_message, PostTask post_task, StatusCallback status_callback);
    ~NativeVoiceCall();
    NativeVoiceCall(const NativeVoiceCall&) = delete;
    NativeVoiceCall& operator=(const NativeVoiceCall&) = delete;

    void SetCapabilities(bool supported, bool requires_headset);
    [[nodiscard]] bool Start(const std::string& device_id, const std::string& stream_id);
    [[nodiscard]] bool SetMicrophoneMuted(bool muted);
    [[nodiscard]] bool SetSpeakerMuted(bool muted);
    void HandleMessage(const std::shared_ptr<px::Message>& message);
    void Stop(bool notify_remote, std::string reason);

  private:
    void HandleResponse(const std::shared_ptr<px::Message>& message);
    void HandleAudioConfig(const std::shared_ptr<px::Message>& message);
    void HandleAudioFrame(const std::shared_ptr<px::Message>& message);
    void QueueAudioFrame(const std::string& call_id, std::uint32_t sequence, std::uint64_t capture_time_ms, const std::vector<std::uint8_t>& opus);
    void DispatchAudioFrame(const std::string& call_id, std::uint32_t sequence, std::uint64_t capture_time_ms, const std::vector<std::uint8_t>& opus);
    void ScheduleTimeout(const std::string& call_id, std::uint64_t request_id);
    void Shutdown(bool notify_remote, std::string reason, bool publish_status);
    void PublishStatus(std::string reason = {});

    mutable std::mutex mutex_{};
    SendMessage send_message_{};
    PostTask post_task_{};
    StatusCallback status_callback_{};
    px::VoiceCallState state_{};
    px::VoicePacketTransport packet_transport_{};
    std::shared_ptr<px::VoiceAudioEndpoint> endpoint_{};
    std::jthread timeout_thread_{};
    std::string device_id_{};
    std::string stream_id_{};
    bool supported_{};
    bool requires_headset_{true};
    bool microphone_muted_{};
    bool speaker_muted_{};
    std::uint64_t inbound_audio_frames_{};
    std::uint64_t rejected_audio_frames_{};
    std::uint64_t endpoint_rejected_audio_frames_{};
};

} // namespace pixels::android
