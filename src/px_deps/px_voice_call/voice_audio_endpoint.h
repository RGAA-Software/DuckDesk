#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "voice_audio_backend.h"

namespace px {

struct VoiceAudioStats {
    uint64_t captured_frames = 0;
    uint64_t encoded_packets = 0;
    uint64_t decoded_packets = 0;
    uint64_t capture_samples_dropped = 0;
    uint64_t playout_samples_dropped = 0;
    uint64_t playout_underruns = 0;
    uint64_t plc_packets = 0;
    uint64_t jitter_duplicates = 0;
    uint64_t jitter_late = 0;
    uint64_t jitter_invalid = 0;
    uint64_t jitter_overflow_drops = 0;
    uint64_t jitter_missing = 0;
    uint64_t apm_capture_frames = 0;
    uint64_t apm_render_frames = 0;
    uint64_t apm_capture_failures = 0;
    uint64_t apm_render_failures = 0;
    size_t jitter_queued_packets = 0;
    size_t jitter_peak_packets = 0;
    uint64_t device_rebuilds = 0;
    uint64_t device_failures = 0;
};

class VoiceAudioEndpoint {
public:
    using BackendFactory = std::function<std::unique_ptr<IVoiceAudioBackend>()>;
    using PacketCallback =
        std::function<void(uint32_t sequence, uint64_t capture_time_ms,
                           const std::vector<uint8_t>& opus)>;
    using ProcessedCaptureCallback =
        std::function<void(const int16_t* samples, size_t sample_count)>;
    using FatalErrorCallback = std::function<void(const std::string& reason)>;

    explicit VoiceAudioEndpoint(BackendFactory backend_factory = {});
    ~VoiceAudioEndpoint();
    VoiceAudioEndpoint(const VoiceAudioEndpoint&) = delete;
    VoiceAudioEndpoint& operator=(const VoiceAudioEndpoint&) = delete;

    bool Start(
        PacketCallback callback, std::string* error,
        FatalErrorCallback fatal_error_callback = {},
        ProcessedCaptureCallback processed_capture_callback = {});
    bool Start(
        PacketCallback callback, const VoiceAudioBackendConfig& backend_config,
        std::string* error, FatalErrorCallback fatal_error_callback = {},
        ProcessedCaptureCallback processed_capture_callback = {});
    void Stop();
    bool ReceiveOpus(
        uint32_t sequence, uint64_t capture_time_ms,
        const void* data, size_t size);
    bool ReceivePcm(
        const int16_t* samples, size_t sample_count,
        int sample_rate, int channels);
    void SetMicrophoneMuted(bool muted);
    void SetSpeakerMuted(bool muted);
    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] VoiceAudioStats Stats() const;
    [[nodiscard]] VoiceAudioBackendInfo BackendInfo() const;

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
