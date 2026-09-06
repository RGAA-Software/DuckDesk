#include "voice_audio_processing.h"

#include <atomic>
#include <mutex>

namespace px {

// Protocol v1 explicitly recommends a headset. Android therefore uses the
// endpoint's Opus/jitter pipeline without pretending software AEC is active;
// a real mobile APM can replace this adapter when the protocol enables it.

class VoiceAudioProcessing::Impl final {
  public:
    bool Initialize(const VoiceAudioProcessingConfig& config) {
        std::lock_guard lock(mutex_);
        config_ = config;
        initialized_ = true;
        capture_frames_.store(0, std::memory_order_relaxed);
        render_frames_.store(0, std::memory_order_relaxed);
        capture_failures_.store(0, std::memory_order_relaxed);
        render_failures_.store(0, std::memory_order_relaxed);
        return true;
    }

    void Reset() {
        std::lock_guard lock(mutex_);
        initialized_ = false;
    }

    bool ProcessCapture(const std::span<std::int16_t> samples) {
        if (!Ready(samples.size())) {
            capture_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        capture_frames_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool ProcessRender(const std::span<const std::int16_t> samples) {
        if (!Ready(samples.size())) {
            render_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        render_frames_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] bool IsInitialized() const {
        std::lock_guard lock(mutex_);
        return initialized_;
    }

    [[nodiscard]] VoiceAudioProcessingConfig Config() const {
        std::lock_guard lock(mutex_);
        return config_;
    }

    [[nodiscard]] VoiceAudioProcessingStats Stats() const {
        return {
            .capture_frames = capture_frames_.load(std::memory_order_relaxed),
            .render_frames = render_frames_.load(std::memory_order_relaxed),
            .capture_failures = capture_failures_.load(std::memory_order_relaxed),
            .render_failures = render_failures_.load(std::memory_order_relaxed),
        };
    }

  private:
    [[nodiscard]] bool Ready(const std::size_t sample_count) const {
        std::lock_guard lock(mutex_);
        return initialized_ && sample_count == VoiceAudioProcessing::kFrameSamples;
    }

    mutable std::mutex mutex_{};
    VoiceAudioProcessingConfig config_{};
    bool initialized_{};
    std::atomic_uint64_t capture_frames_{};
    std::atomic_uint64_t render_frames_{};
    std::atomic_uint64_t capture_failures_{};
    std::atomic_uint64_t render_failures_{};
};

VoiceAudioProcessing::VoiceAudioProcessing() : impl_(std::make_unique<Impl>()) {}
VoiceAudioProcessing::~VoiceAudioProcessing() = default;

bool VoiceAudioProcessing::Initialize(const VoiceAudioProcessingConfig& config) {
    return impl_->Initialize(config);
}

void VoiceAudioProcessing::Reset() {
    impl_->Reset();
}

bool VoiceAudioProcessing::ProcessCapture(const std::span<std::int16_t> samples) {
    return impl_->ProcessCapture(samples);
}

bool VoiceAudioProcessing::ProcessRender(const std::span<const std::int16_t> samples) {
    return impl_->ProcessRender(samples);
}

bool VoiceAudioProcessing::IsInitialized() const {
    return impl_->IsInitialized();
}

VoiceAudioProcessingConfig VoiceAudioProcessing::Config() const {
    return impl_->Config();
}

VoiceAudioProcessingStats VoiceAudioProcessing::Stats() const {
    return impl_->Stats();
}

} // namespace px
