#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace px {

struct VoiceAudioStats {
    uint64_t captured_frames = 0;
    uint64_t encoded_packets = 0;
    uint64_t decoded_packets = 0;
    uint64_t capture_samples_dropped = 0;
    uint64_t playout_samples_dropped = 0;
    uint64_t playout_underruns = 0;
};

class VoiceAudioEndpoint {
public:
    using PacketCallback =
        std::function<void(uint32_t sequence, uint64_t capture_time_ms,
                           const std::vector<uint8_t>& opus)>;

    VoiceAudioEndpoint();
    ~VoiceAudioEndpoint();
    VoiceAudioEndpoint(const VoiceAudioEndpoint&) = delete;
    VoiceAudioEndpoint& operator=(const VoiceAudioEndpoint&) = delete;

    bool Start(PacketCallback callback, std::string* error);
    void Stop();
    bool ReceiveOpus(const void* data, size_t size);
    void SetMicrophoneMuted(bool muted);
    void SetSpeakerMuted(bool muted);
    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] VoiceAudioStats Stats() const;

    static constexpr int kSampleRate = 48'000;
    static constexpr int kChannels = 1;
    static constexpr int kBitsPerSample = 16;
    static constexpr int kFrameMs = 20;
    static constexpr int kFrameSamples = kSampleRate * kFrameMs / 1000;
    static constexpr int kBitrateBps = 32'000;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace px
