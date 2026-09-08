#pragma once

#include <functional>
#include <string>

#include "px_voice_call/voice_audio_stats.h"
#include "px_voice_call/voice_packet_transport.h"

namespace px {

// One owned device session per call. The SDK governs consent/lifetime; the host selects its real audio backend.
// A port is fully constructed before Start; Stop must be safe after every partial failure and repeated calls.
class VoiceAudioPort {
  public:
    using EncodedCallback = std::function<void(VoiceTransportPacket)>;
    using ErrorCallback = std::function<void(std::string)>;

    virtual ~VoiceAudioPort() = default;
    virtual bool Start(EncodedCallback encoded, ErrorCallback error, std::string& reason) = 0;
    virtual void Stop() = 0;
    virtual bool Receive(const VoiceTransportPacket& packet) = 0;
    virtual void SetMicrophoneMuted(bool muted) = 0;
    virtual void SetSpeakerMuted(bool muted) = 0;
    [[nodiscard]] virtual VoiceAudioStats Stats() const = 0;
};

} // namespace px
