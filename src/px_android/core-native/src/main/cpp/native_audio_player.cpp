#include "native_audio_player.h"

#include "px_common/data.h"

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace pixels::android {
namespace {

struct AudioStreamBuilderReleaser final {
    void operator()(AAudioStreamBuilder* builder) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        if (builder != nullptr)
            AAudioStreamBuilder_delete(builder);
    }
};

struct AudioStreamReleaser final {
    void operator()(AAudioStream* stream) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        if (stream != nullptr)
            AAudioStream_close(stream);
    }
};

using AudioStreamBuilderHandle = std::unique_ptr<AAudioStreamBuilder, AudioStreamBuilderReleaser>;
using AudioStreamHandle = std::unique_ptr<AAudioStream, AudioStreamReleaser>;

AudioStreamBuilderHandle CreateBuilder() {
    AAudioStreamBuilder* builder{}; // NOLINT(gammaray-raw-pointer-boundary)
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
        return {};
    return AudioStreamBuilderHandle{builder};
}

AudioStreamHandle OpenStream(const std::int32_t sample_rate, const std::int32_t channels) {
    auto builder = CreateBuilder();
    if (!builder)
        return {};
    AAudioStreamBuilder_setDirection(builder.get(), AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder.get(), AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder.get(), sample_rate);
    AAudioStreamBuilder_setChannelCount(builder.get(), channels);
    AAudioStreamBuilder_setSharingMode(builder.get(), AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder.get(), AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setUsage(builder.get(), AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(builder.get(), AAUDIO_CONTENT_TYPE_MOVIE);

    AAudioStream* stream{}; // NOLINT(gammaray-raw-pointer-boundary)
    if (AAudioStreamBuilder_openStream(builder.get(), &stream) != AAUDIO_OK)
        return {};
    AudioStreamHandle result{stream};
    if (AAudioStream_requestStart(result.get()) != AAUDIO_OK)
        return {};
    return result;
}

} // namespace

class NativeAudioPlayer::Impl final {
  public:
    bool Write(const std::shared_ptr<px::Data>& pcm, const std::int32_t sample_rate, const std::int32_t channels,
               const std::int32_t bits_per_sample) {
        if (!enabled_.load() || !pcm || pcm->Size() == 0 || sample_rate <= 0 || channels <= 0 || channels > 8 || bits_per_sample != 16) {
            return false;
        }
        std::lock_guard command_lock(command_mutex_);
        if (!enabled_.load())
            return false;
        if (!stream_ || sample_rate_ != sample_rate || channels_ != channels) {
            CloseStream();
            stream_ = OpenStream(sample_rate, channels);
            if (!stream_)
                return false;
            sample_rate_ = sample_rate;
            channels_ = channels;
        }
        const auto bytes = pcm->Bytes();
        const auto bytes_per_frame = static_cast<std::size_t>(channels) * sizeof(std::int16_t);
        if (bytes.size() % bytes_per_frame != 0)
            return false;
        const auto frame_count = static_cast<std::int32_t>(bytes.size() / bytes_per_frame);
        constexpr std::int64_t kWriteTimeoutNanos = 10'000'000;
        const auto written = AAudioStream_write(stream_.get(), bytes.data(), frame_count, kWriteTimeoutNanos);
        if (written >= 0)
            return written == frame_count;
        CloseStream();
        return false;
    }

    void SetEnabled(const bool enabled) {
        enabled_.store(enabled);
        if (!enabled)
            Stop();
    }

    void Stop() {
        std::lock_guard command_lock(command_mutex_);
        CloseStream();
    }

  private:
    void CloseStream() {
        if (stream_)
            AAudioStream_requestStop(stream_.get());
        stream_.reset();
        sample_rate_ = 0;
        channels_ = 0;
    }

    std::mutex command_mutex_{};
    AudioStreamHandle stream_{};
    std::atomic_bool enabled_{true};
    std::int32_t sample_rate_{};
    std::int32_t channels_{};
};

NativeAudioPlayer::NativeAudioPlayer() : impl_(std::make_unique<Impl>()) {}

NativeAudioPlayer::~NativeAudioPlayer() = default;

bool NativeAudioPlayer::Write(const std::shared_ptr<px::Data>& pcm, const std::int32_t sample_rate, const std::int32_t channels,
                              const std::int32_t bits_per_sample) {
    return impl_->Write(pcm, sample_rate, channels, bits_per_sample);
}

void NativeAudioPlayer::SetEnabled(const bool enabled) {
    impl_->SetEnabled(enabled);
}

void NativeAudioPlayer::Stop() {
    impl_->Stop();
}

} // namespace pixels::android
