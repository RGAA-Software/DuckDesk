#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace px {

#if defined(_WIN32)
#if defined(PX_VOICE_APM_EXPORTS)
#define PX_VOICE_APM_API __declspec(dllexport)
#else
#define PX_VOICE_APM_API __declspec(dllimport)
#endif
#else
#define PX_VOICE_APM_API
#endif

struct VoiceAudioProcessingConfig {
    bool echo_cancellation = true;
    bool noise_suppression = true;
    bool automatic_gain_control = true;
};

struct VoiceAudioProcessingStats {
    uint64_t capture_frames = 0;
    uint64_t render_frames = 0;
    uint64_t capture_failures = 0;
    uint64_t render_failures = 0;
};

// Owns one libwebrtc Audio Processing Module per call. Both methods require
// exactly one 10 ms, 48 kHz mono frame. The render input must be the PCM that
// is actually sent to the local output device (after local mute/volume).
class PX_VOICE_APM_API VoiceAudioProcessing {
public:
    static constexpr int kSampleRate = 48'000;
    static constexpr size_t kFrameSamples = kSampleRate / 100;

    VoiceAudioProcessing();
    ~VoiceAudioProcessing();
    VoiceAudioProcessing(const VoiceAudioProcessing&) = delete;
    VoiceAudioProcessing& operator=(const VoiceAudioProcessing&) = delete;

    bool Initialize(const VoiceAudioProcessingConfig& config);
    void Reset();
    bool ProcessCapture(std::span<int16_t> samples);
    bool ProcessRender(std::span<const int16_t> samples);

    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] VoiceAudioProcessingConfig Config() const;
    [[nodiscard]] VoiceAudioProcessingStats Stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace px
