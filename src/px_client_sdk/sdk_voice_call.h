#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "sdk_voice_audio_port.h"
#include "sdk_voice_protocol.h"
#include "px_voice_call/voice_call_state.h"

namespace px {

struct VoiceCallStatus final {
    VoiceCallPhase phase{VoiceCallPhase::kIdle};
    bool supported{};
    bool requires_headset{true};
    bool microphone_muted{};
    bool speaker_muted{};
    std::uint64_t revision{};
    std::string reason{};
};

struct VoiceCallConfig final {
    std::string device_id{};
    std::string stream_id{};
    std::chrono::milliseconds request_timeout{VoiceCallState::kRequestTimeoutMs};
};

struct VoiceCallDependencies final {
    using MessageSender = std::function<bool(std::shared_ptr<Message>)>;
    MessageSender send_control{};
    MessageSender send_audio{};
    std::function<std::shared_ptr<VoiceAudioPort>()> create_audio{};
    std::function<bool(std::function<void()>)> post_task{};
    std::function<void(const VoiceCallStatus&)> status_changed{};
};

class VoiceCallController final : public std::enable_shared_from_this<VoiceCallController> {
  private:
    struct ConstructionKey final {};

  public:
    static std::shared_ptr<VoiceCallController> Create(VoiceCallConfig config, VoiceCallDependencies dependencies);
    VoiceCallController(ConstructionKey, VoiceCallConfig config, VoiceCallDependencies dependencies);
    ~VoiceCallController();
    VoiceCallController(const VoiceCallController&) = delete;
    VoiceCallController& operator=(const VoiceCallController&) = delete;

    void SetCapabilities(bool supported, bool requires_headset);
    void SetTransportAvailable(bool available);
    [[nodiscard]] bool Start();
    void HandleMessage(const std::shared_ptr<Message>& message);
    [[nodiscard]] bool SetMicrophoneMuted(bool muted);
    [[nodiscard]] bool SetSpeakerMuted(bool muted);
    void Stop(bool notify_remote, std::string reason);
    void Close();
    [[nodiscard]] VoiceCallStatus Status() const;

  private:
    class Run;
    void RunChanged(const std::shared_ptr<Run>& run, std::string reason);
    void ScheduleStatus(std::uint64_t revision);

    const VoiceCallConfig config_{};
    const VoiceCallDependencies dependencies_{};
    mutable std::mutex mutex_{};
    VoiceCallRequestSequence request_sequence_{};
    std::shared_ptr<Run> active_{};
    VoiceCallStatus status_{};
    bool closed_{};
    bool transport_available_{true};
};

} // namespace px
