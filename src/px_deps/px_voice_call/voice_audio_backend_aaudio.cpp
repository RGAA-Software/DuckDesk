#include "voice_audio_backend.h"

#include <aaudio/AAudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace px {
namespace {

struct AudioStreamBuilderReleaser final {
    void operator()(AAudioStreamBuilder* builder) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): AAudio ownership boundary.
        if (builder != nullptr) {
            AAudioStreamBuilder_delete(builder);
        }
    }
};

struct AudioStreamReleaser final {
    void operator()(AAudioStream* stream) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): AAudio ownership boundary.
        if (stream != nullptr) {
            AAudioStream_close(stream);
        }
    }
};

using AudioStreamBuilderHandle = std::unique_ptr<AAudioStreamBuilder, AudioStreamBuilderReleaser>;
using AudioStreamHandle = std::shared_ptr<AAudioStream>;

AudioStreamBuilderHandle CreateBuilder() {
    AAudioStreamBuilder* builder{}; // NOLINT(gammaray-raw-pointer-boundary): transient AAudio out parameter immediately wrapped in RAII.
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
        return {};
    }
    return AudioStreamBuilderHandle{builder};
}

AudioStreamHandle OpenStream(const VoiceAudioBackendConfig& config, const aaudio_direction_t direction) {
    auto builder = CreateBuilder();
    if (!builder) {
        return {};
    }
    AAudioStreamBuilder_setDirection(builder.get(), direction);
    AAudioStreamBuilder_setFormat(builder.get(), AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder.get(), static_cast<std::int32_t>(config.sample_rate));
    AAudioStreamBuilder_setChannelCount(builder.get(), static_cast<std::int32_t>(config.channels));
    AAudioStreamBuilder_setSharingMode(builder.get(), AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder.get(), AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    if (direction == AAUDIO_DIRECTION_INPUT) {
        AAudioStreamBuilder_setInputPreset(builder.get(), AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);
    } else {
        AAudioStreamBuilder_setUsage(builder.get(), AAUDIO_USAGE_VOICE_COMMUNICATION);
        AAudioStreamBuilder_setContentType(builder.get(), AAUDIO_CONTENT_TYPE_SPEECH);
    }
    AAudioStream* stream{}; // NOLINT(gammaray-raw-pointer-boundary): transient AAudio out parameter immediately wrapped in RAII.
    if (AAudioStreamBuilder_openStream(builder.get(), &stream) != AAUDIO_OK) {
        return {};
    }
    AudioStreamHandle result{stream, AudioStreamReleaser{}};
    if (AAudioStream_getFormat(result.get()) != AAUDIO_FORMAT_PCM_I16 ||
        AAudioStream_getChannelCount(result.get()) != static_cast<std::int32_t>(config.channels) ||
        AAudioStream_getSampleRate(result.get()) != static_cast<std::int32_t>(config.sample_rate)) {
        return {};
    }
    return result;
}

class AAudioVoiceAudioBackend final : public IVoiceAudioBackend {
  public:
    ~AAudioVoiceAudioBackend() override {
        Stop();
    }

    bool Start(const VoiceAudioBackendConfig& config, CaptureCallback capture_callback, PlayoutCallback playout_callback,
               EventCallback event_callback, std::string& error) override {
        Stop();
        if ((!config.capture_enabled && !config.playout_enabled) || config.sample_rate != 48'000 || config.channels != 1 ||
            config.frames_per_callback == 0 || config.frames_per_callback > 4'800 || (config.capture_enabled && !capture_callback) ||
            (config.playout_enabled && !playout_callback)) {
            error = "invalid AAudio voice backend configuration";
            return false;
        }

        auto state = std::make_shared<WorkerState>();
        state->config = config;
        state->capture_callback = std::move(capture_callback);
        state->playout_callback = std::move(playout_callback);
        state->event_callback = std::move(event_callback);
        if (config.capture_enabled) {
            state->capture_stream = OpenStream(config, AAUDIO_DIRECTION_INPUT);
            if (!state->capture_stream) {
                error = "default microphone unavailable";
                return false;
            }
        }
        if (config.playout_enabled) {
            state->playout_stream = OpenStream(config, AAUDIO_DIRECTION_OUTPUT);
            if (!state->playout_stream) {
                error = "communications output unavailable";
                return false;
            }
        }
        if ((state->capture_stream && AAudioStream_requestStart(state->capture_stream.get()) != AAUDIO_OK) ||
            (state->playout_stream && AAudioStream_requestStart(state->playout_stream.get()) != AAUDIO_OK)) {
            error = "AAudio voice stream failed to start";
            return false;
        }
        state->running.store(true, std::memory_order_release);
        {
            std::lock_guard lock(mutex_);
            state_ = state;
            if (state->capture_stream) {
                capture_thread_ = std::jthread([state](const std::stop_token token) { CaptureMain(state, token); });
            }
            if (state->playout_stream) {
                playout_thread_ = std::jthread([state](const std::stop_token token) { PlayoutMain(state, token); });
            }
        }
        return true;
    }

    void Stop() override {
        std::shared_ptr<WorkerState> state;
        std::jthread capture_thread;
        std::jthread playout_thread;
        {
            std::lock_guard lock(mutex_);
            state = std::move(state_);
            capture_thread = std::move(capture_thread_);
            playout_thread = std::move(playout_thread_);
        }
        if (!state) {
            return;
        }
        state->running.store(false, std::memory_order_release);
        capture_thread.request_stop();
        playout_thread.request_stop();
        AudioStreamHandle capture_stream;
        AudioStreamHandle playout_stream;
        {
            std::lock_guard stream_lock(state->stream_mutex);
            capture_stream = state->capture_stream;
            playout_stream = state->playout_stream;
        }
        if (capture_stream) {
            AAudioStream_requestStop(capture_stream.get());
        }
        if (playout_stream) {
            AAudioStream_requestStop(playout_stream.get());
        }
        if (capture_thread.joinable()) {
            capture_thread.join();
        }
        if (playout_thread.joinable()) {
            playout_thread.join();
        }
        {
            std::lock_guard stream_lock(state->stream_mutex);
            state->capture_stream.reset();
            state->playout_stream.reset();
        }
    }

    bool EnumerateDevices(VoiceAudioDeviceInventory& inventory, std::string& error) override {
        error.clear();
        inventory = {
            .capture_devices = {{.id = "default", .name = "System communications microphone", .is_default = true}},
            .playout_devices = {{.id = "default", .name = "System communications output", .is_default = true}},
        };
        return true;
    }

    [[nodiscard]] bool IsRunning() const override {
        std::lock_guard lock(mutex_);
        return state_ && state_->running.load(std::memory_order_acquire);
    }

    [[nodiscard]] VoiceAudioBackendInfo Info() const override {
        return {.backend = "AAudio", .capture_device = "system_communications_microphone", .playout_device = "system_communications_output"};
    }

  private:
    struct WorkerState final {
        VoiceAudioBackendConfig config{};
        CaptureCallback capture_callback{};
        PlayoutCallback playout_callback{};
        EventCallback event_callback{};
        AudioStreamHandle capture_stream{};
        AudioStreamHandle playout_stream{};
        std::mutex stream_mutex{};
        std::atomic_bool running{};
        std::atomic_bool failure_reported{};
    };

    static void ReportFailure(const std::shared_ptr<WorkerState>& state, const std::string& reason) {
        state->running.store(false, std::memory_order_release);
        if (!state->failure_reported.exchange(true, std::memory_order_acq_rel) && state->event_callback) {
            state->event_callback(VoiceAudioBackendEvent::kStopped, reason);
        }
    }

    static AudioStreamHandle CurrentStream(const std::shared_ptr<WorkerState>& state, const aaudio_direction_t direction) {
        std::lock_guard lock(state->stream_mutex);
        return direction == AAUDIO_DIRECTION_INPUT ? state->capture_stream : state->playout_stream;
    }

    static bool RecoverStream(const std::shared_ptr<WorkerState>& state, const aaudio_direction_t direction, const std::stop_token token) {
        constexpr auto kRetryDelay = std::chrono::milliseconds(50);
        constexpr std::size_t kMaxAttempts = 3;
        for (std::size_t attempt{}; attempt < kMaxAttempts && !token.stop_requested() && state->running.load(std::memory_order_acquire); ++attempt) {
            auto replacement = OpenStream(state->config, direction);
            if (replacement && AAudioStream_requestStart(replacement.get()) == AAUDIO_OK) {
                {
                    std::lock_guard lock(state->stream_mutex);
                    auto& destination = direction == AAUDIO_DIRECTION_INPUT ? state->capture_stream : state->playout_stream;
                    destination = std::move(replacement);
                }
                if (state->event_callback) {
                    state->event_callback(VoiceAudioBackendEvent::kRerouted, "communications_route_changed");
                }
                return true;
            }
            std::this_thread::sleep_for(kRetryDelay);
        }
        return false;
    }

    static void CaptureMain(const std::shared_ptr<WorkerState>& state, const std::stop_token token) {
        std::vector<std::int16_t> samples(state->config.frames_per_callback * state->config.channels);
        constexpr std::int64_t kReadTimeoutNanos = 100'000'000;
        while (!token.stop_requested() && state->running.load(std::memory_order_acquire)) {
            const auto stream = CurrentStream(state, AAUDIO_DIRECTION_INPUT);
            if (!stream) {
                ReportFailure(state, "microphone_stream_lost");
                return;
            }
            const auto frames =
                AAudioStream_read(stream.get(), samples.data(), static_cast<std::int32_t>(state->config.frames_per_callback), kReadTimeoutNanos);
            if (frames == AAUDIO_ERROR_TIMEOUT) {
                continue;
            }
            if (frames < 0) {
                if (!RecoverStream(state, AAUDIO_DIRECTION_INPUT, token)) {
                    ReportFailure(state, "microphone_stream_lost");
                    return;
                }
                continue;
            }
            if (frames > 0 && state->capture_callback) {
                state->capture_callback(std::span<const std::int16_t>(samples.data(), static_cast<std::size_t>(frames) * state->config.channels));
            }
        }
    }

    static void PlayoutMain(const std::shared_ptr<WorkerState>& state, const std::stop_token token) {
        std::vector<std::int16_t> samples(state->config.frames_per_callback * state->config.channels);
        constexpr std::int64_t kWriteTimeoutNanos = 100'000'000;
        while (!token.stop_requested() && state->running.load(std::memory_order_acquire)) {
            std::ranges::fill(samples, std::int16_t{});
            if (state->playout_callback) {
                state->playout_callback(samples);
            }
            const auto stream = CurrentStream(state, AAUDIO_DIRECTION_OUTPUT);
            if (!stream) {
                ReportFailure(state, "playout_stream_lost");
                return;
            }
            const auto frames =
                AAudioStream_write(stream.get(), samples.data(), static_cast<std::int32_t>(state->config.frames_per_callback), kWriteTimeoutNanos);
            if (frames == AAUDIO_ERROR_TIMEOUT) {
                continue;
            }
            if (frames < 0) {
                if (!RecoverStream(state, AAUDIO_DIRECTION_OUTPUT, token)) {
                    ReportFailure(state, "playout_stream_lost");
                    return;
                }
            }
        }
    }

    mutable std::mutex mutex_{};
    std::shared_ptr<WorkerState> state_{};
    std::jthread capture_thread_{};
    std::jthread playout_thread_{};
};

} // namespace

std::unique_ptr<IVoiceAudioBackend> CreateAAudioVoiceAudioBackend() {
    return std::make_unique<AAudioVoiceAudioBackend>();
}

} // namespace px
