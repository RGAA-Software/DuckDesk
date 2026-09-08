#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "voice_audio_backend.h"
#include "voice_audio_format.h"
#include "voice_audio_stats.h"

namespace px {


class VoiceAudioEndpoint {
public:
    using BackendFactory = std::function<std::unique_ptr<IVoiceAudioBackend>()>;
    using PacketCallback =
        std::function<void(uint32_t sequence, uint64_t capture_time_ms,
                           const std::vector<uint8_t>& opus)>;
    using ProcessedCaptureCallback =
        std::function<void(std::span<const int16_t> samples)>;
    using FatalErrorCallback = std::function<void(const std::string& reason)>;

    explicit VoiceAudioEndpoint(BackendFactory backend_factory = {});
    ~VoiceAudioEndpoint();
    VoiceAudioEndpoint(const VoiceAudioEndpoint&) = delete;
    VoiceAudioEndpoint& operator=(const VoiceAudioEndpoint&) = delete;

    bool Start(
        PacketCallback callback, std::string& error,
        FatalErrorCallback fatal_error_callback = {},
        ProcessedCaptureCallback processed_capture_callback = {});
    bool Start(
        PacketCallback callback, const VoiceAudioBackendConfig& backend_config,
        std::string& error, FatalErrorCallback fatal_error_callback = {},
        ProcessedCaptureCallback processed_capture_callback = {});
    void Stop();
    bool ReceiveOpus(
        uint32_t sequence, uint64_t capture_time_ms,
        std::span<const uint8_t> data);
    bool ReceivePcm(
        std::span<const int16_t> samples,
        int sample_rate, int channels);
    void SetMicrophoneMuted(bool muted);
    void SetSpeakerMuted(bool muted);
    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] VoiceAudioStats Stats() const;
    [[nodiscard]] VoiceAudioBackendInfo BackendInfo() const;

    static constexpr int kSampleRate = VoiceAudioFormat::kSampleRate;
    static constexpr int kChannels = VoiceAudioFormat::kChannels;
    static constexpr int kBitsPerSample = VoiceAudioFormat::kBitsPerSample;
    static constexpr int kFrameMs = VoiceAudioFormat::kFrameMs;
    static constexpr int kFrameSamples = VoiceAudioFormat::kFrameSamples;
    static constexpr int kBitrateBps = VoiceAudioFormat::kBitrateBps;

  private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace px
