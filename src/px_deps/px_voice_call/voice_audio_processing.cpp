#include "voice_audio_processing.h"

#include <array>
#include <atomic>
#include <mutex>
#include <utility>

#include "modules/audio_processing/include/audio_processing.h"

namespace px {

class VoiceAudioProcessing::Impl {
public:
    bool Initialize(const VoiceAudioProcessingConfig& config) {
        std::scoped_lock lock(mutex_);

        webrtc::AudioProcessing::Config apm_config;
        apm_config.echo_canceller.enabled = config.echo_cancellation;
        apm_config.echo_canceller.mobile_mode = false;
        apm_config.noise_suppression.enabled = config.noise_suppression;
        apm_config.noise_suppression.level =
            webrtc::AudioProcessing::Config::NoiseSuppression::kHigh;
        apm_config.high_pass_filter.enabled = true;
        apm_config.gain_controller1.enabled = false;
        apm_config.gain_controller2.enabled = config.automatic_gain_control;
        apm_config.gain_controller2.input_volume_controller.enabled = false;
        apm_config.gain_controller2.adaptive_digital.enabled =
            config.automatic_gain_control;

        auto apm = webrtc::AudioProcessingBuilder()
                       .SetConfig(apm_config)
                       .Create();
        if (!apm) {
            return false;
        }
        const webrtc::ProcessingConfig processing_config{
            webrtc::StreamConfig(kSampleRate, 1),
            webrtc::StreamConfig(kSampleRate, 1),
            webrtc::StreamConfig(kSampleRate, 1),
            webrtc::StreamConfig(kSampleRate, 1),
        };
        if (apm->Initialize(processing_config) != webrtc::AudioProcessing::kNoError) {
            return false;
        }

        apm_ = std::move(apm);
        config_ = config;
        capture_frames_ = 0;
        render_frames_ = 0;
        capture_failures_ = 0;
        render_failures_ = 0;
        return true;
    }

    void Reset() {
        std::scoped_lock lock(mutex_);
        apm_ = nullptr;
    }

    bool ProcessCapture(int16_t* samples, size_t sample_count) {
        if (!samples || sample_count != kFrameSamples) {
            capture_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::scoped_lock lock(mutex_);
        if (!apm_) {
            capture_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const webrtc::StreamConfig stream(kSampleRate, 1);
        const int result = apm_->ProcessStream(samples, stream, stream, samples);
        if (result != webrtc::AudioProcessing::kNoError) {
            capture_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        capture_frames_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool ProcessRender(const int16_t* samples, size_t sample_count) {
        if (!samples || sample_count != kFrameSamples) {
            render_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::scoped_lock lock(mutex_);
        if (!apm_) {
            render_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const webrtc::StreamConfig stream(kSampleRate, 1);
        std::array<int16_t, kFrameSamples> processed{};
        const int result = apm_->ProcessReverseStream(
            samples, stream, stream, processed.data());
        if (result != webrtc::AudioProcessing::kNoError) {
            render_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        render_frames_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] bool IsInitialized() const {
        std::scoped_lock lock(mutex_);
        return apm_ != nullptr;
    }

    [[nodiscard]] VoiceAudioProcessingConfig Config() const {
        std::scoped_lock lock(mutex_);
        return config_;
    }

    [[nodiscard]] VoiceAudioProcessingStats Stats() const {
        return VoiceAudioProcessingStats{
            .capture_frames = capture_frames_.load(std::memory_order_relaxed),
            .render_frames = render_frames_.load(std::memory_order_relaxed),
            .capture_failures = capture_failures_.load(std::memory_order_relaxed),
            .render_failures = render_failures_.load(std::memory_order_relaxed),
        };
    }

private:
    mutable std::mutex mutex_;
    rtc::scoped_refptr<webrtc::AudioProcessing> apm_;
    VoiceAudioProcessingConfig config_;
    std::atomic_uint64_t capture_frames_ = 0;
    std::atomic_uint64_t render_frames_ = 0;
    std::atomic_uint64_t capture_failures_ = 0;
    std::atomic_uint64_t render_failures_ = 0;
};

VoiceAudioProcessing::VoiceAudioProcessing() : impl_(std::make_unique<Impl>()) {}
VoiceAudioProcessing::~VoiceAudioProcessing() = default;

bool VoiceAudioProcessing::Initialize(const VoiceAudioProcessingConfig& config) {
    return impl_->Initialize(config);
}

void VoiceAudioProcessing::Reset() { impl_->Reset(); }

bool VoiceAudioProcessing::ProcessCapture(int16_t* samples, size_t sample_count) {
    return impl_->ProcessCapture(samples, sample_count);
}

bool VoiceAudioProcessing::ProcessRender(
    const int16_t* samples, size_t sample_count) {
    return impl_->ProcessRender(samples, sample_count);
}

bool VoiceAudioProcessing::IsInitialized() const { return impl_->IsInitialized(); }
VoiceAudioProcessingConfig VoiceAudioProcessing::Config() const { return impl_->Config(); }
VoiceAudioProcessingStats VoiceAudioProcessing::Stats() const { return impl_->Stats(); }

}  // namespace px
