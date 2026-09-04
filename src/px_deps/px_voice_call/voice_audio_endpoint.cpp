#include "voice_audio_endpoint.h"
#include "px_common_new/async_runtime.h"

#include <opus/opus.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

#include "px_opus_codec_new/opus_codec.h"
#include "voice_audio_backend.h"
#include "voice_audio_processing.h"
#include "voice_jitter_buffer.h"

namespace px {
namespace {

uint64_t MonotonicMillis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

class BoundedPcmQueue {
public:
    explicit BoundedPcmQueue(size_t capacity_samples) : storage_(capacity_samples) {}

    uint64_t Write(std::span<const int16_t> samples) {
        if (samples.empty() || storage_.empty()) {
            return 0;
        }
        std::scoped_lock lock(mutex_);
        return WriteLocked(samples);
    }

    uint64_t WriteSilence(size_t count) {
        if (count == 0 || storage_.empty()) {
            return 0;
        }
        std::scoped_lock lock(mutex_);
        uint64_t dropped = 0;
        while (count > 0) {
            const size_t chunk = std::min(count, silence_.size());
            dropped += WriteLocked(
                std::span<const int16_t>(silence_).first(chunk));
            count -= chunk;
        }
        return dropped;
    }

    size_t Read(std::span<int16_t> destination) {
        if (destination.empty()) {
            return 0;
        }
        std::scoped_lock lock(mutex_);
        const auto available = std::min(destination.size(), size_);
        for (size_t i = 0; i < available; ++i) {
            destination[i] = storage_[read_];
            read_ = (read_ + 1) % storage_.size();
        }
        size_ -= available;
        return available;
    }

    [[nodiscard]] size_t Size() const {
        std::scoped_lock lock(mutex_);
        return size_;
    }

    void Clear() {
        std::scoped_lock lock(mutex_);
        read_ = write_ = size_ = 0;
    }

private:
    uint64_t WriteLocked(std::span<const int16_t> samples) {
        uint64_t dropped = 0;
        if (samples.size() >= storage_.size()) {
            dropped += size_ + samples.size() - storage_.size();
            samples = samples.last(storage_.size());
            read_ = write_ = size_ = 0;
        }
        else if (size_ + samples.size() > storage_.size()) {
            const auto remove = size_ + samples.size() - storage_.size();
            read_ = (read_ + remove) % storage_.size();
            size_ -= remove;
            dropped += remove;
        }
        for (size_t i = 0; i < samples.size(); ++i) {
            storage_[write_] = samples[i];
            write_ = (write_ + 1) % storage_.size();
        }
        size_ += samples.size();
        return dropped;
    }

    mutable std::mutex mutex_;
    std::vector<int16_t> storage_;
    std::array<int16_t, 480> silence_{};
    size_t read_ = 0;
    size_t write_ = 0;
    size_t size_ = 0;
};

}  // namespace

class VoiceAudioEndpoint::Impl final :
    public std::enable_shared_from_this<VoiceAudioEndpoint::Impl> {
public:
    static constexpr size_t kTenMsSamples = kSampleRate / 100;

    explicit Impl(BackendFactory backend_factory)
        : capture_queue_(kTenMsSamples * 10),
          reverse_queue_(kTenMsSamples * 10),
          playout_queue_(kFrameSamples * 10),
          backend_factory_(std::move(backend_factory)) {}

    ~Impl() { Stop(); }

    bool Start(
        PacketCallback callback, const VoiceAudioBackendConfig& backend_config,
        std::string& error,
        FatalErrorCallback fatal_error_callback,
        ProcessedCaptureCallback processed_capture_callback) {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        StopLocked();
        if (!callback) {
            SetError(error, "voice packet callback is empty");
            return false;
        }
        if (!audio_processing_.Initialize({})) {
            SetError(error, "libwebrtc AudioProcessing initialization failed");
            return false;
        }
        encoder_ = std::make_unique<OpusAudioEncoder>(
            kSampleRate, kChannels, kBitsPerSample, OPUS_APPLICATION_VOIP, 10);
        decoder_ = std::make_unique<OpusAudioDecoder>(kSampleRate, kChannels);
        if (!encoder_->valid() || !decoder_->valid()) {
            SetError(error, "Opus voice codec initialization failed");
            ResetProcessing();
            return false;
        }
        encoder_->SetBitrate(kBitrateBps);
        encoder_->SetComplexity(8);
        callback_ = std::move(callback);
        fatal_error_callback_ = std::move(fatal_error_callback);
        processed_capture_callback_ = std::move(processed_capture_callback);

        ResetStats();
        next_sequence_ = 0;
        microphone_muted_ = false;
        speaker_muted_ = false;
        fatal_error_signaled_ = false;
        running_ = true;
        backend_ = backend_factory_
            ? backend_factory_()
            : CreateDefaultVoiceAudioBackend();
        auto resolved_backend_config = backend_config;
        resolved_backend_config.sample_rate = kSampleRate;
        resolved_backend_config.channels = kChannels;
        resolved_backend_config.frames_per_callback = static_cast<uint32_t>(kTenMsSamples);
        const auto weak_self = weak_from_this();
        if (!backend_ || !backend_->Start(
                resolved_backend_config,
                [weak_self](std::span<const int16_t> samples) {
                    if (const auto self = weak_self.lock()) {
                        self->OnCapture(samples);
                    }
                },
                [weak_self](std::span<int16_t> samples) {
                    if (const auto self = weak_self.lock()) {
                        self->OnPlayout(samples);
                    }
                },
                [weak_self](
                    VoiceAudioBackendEvent event,
                    const std::string& reason) {
                    if (const auto self = weak_self.lock()) {
                        self->OnBackendEvent(event, reason);
                    }
                }, error)) {
            running_ = false;
            backend_.reset();
            ResetProcessing();
            return false;
        }
        const auto self = shared_from_this();
        encode_worker_ = std::thread([self] { self->EncodeWorker(); });
        decode_worker_ = std::thread([self] { self->DecodeWorker(); });
        return true;
    }

    void Stop() {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        StopLocked();
    }

    void StopLocked() {
        const bool was_running = running_.exchange(false);
        if (backend_) {
            backend_->Stop();
        }
        capture_cv_.notify_all();
        jitter_cv_.notify_all();
        if (encode_worker_.joinable()) {
            if (encode_worker_.get_id() == std::this_thread::get_id()) {
                PxAsyncRuntime::DeferJoin(std::move(encode_worker_));
            }
            else {
                encode_worker_.join();
            }
        }
        if (decode_worker_.joinable()) {
            if (decode_worker_.get_id() == std::this_thread::get_id()) {
                PxAsyncRuntime::DeferJoin(std::move(decode_worker_));
            }
            else {
                decode_worker_.join();
            }
        }
        if (was_running || encoder_ || decoder_) {
            capture_queue_.Clear();
            reverse_queue_.Clear();
            playout_queue_.Clear();
            ResetProcessing();
        }
        backend_.reset();
        callback_ = {};
        fatal_error_callback_ = {};
        processed_capture_callback_ = {};
    }

    bool ReceiveOpus(
        uint32_t sequence, uint64_t capture_time_ms,
        std::span<const uint8_t> data) {
        if (!running_ || data.empty()) {
            return false;
        }
        VoiceEncodedPacket packet{
            .sequence = sequence,
            .capture_time_ms = capture_time_ms,
            .arrival_time_ms = MonotonicMillis(),
            .opus = std::vector<uint8_t>(data.begin(), data.end()),
        };
        VoiceJitterPushResult result;
        {
            std::scoped_lock lock(jitter_mutex_);
            result = jitter_buffer_.Push(std::move(packet));
        }
        if (result == VoiceJitterPushResult::kAccepted) {
            jitter_cv_.notify_one();
            return true;
        }
        return false;
    }

    bool ReceivePcm(
        std::span<const int16_t> samples,
        int sample_rate, int channels) {
        if (!running_ || samples.empty() ||
            sample_rate != kSampleRate || channels <= 0 || channels > 8 ||
            samples.size() % static_cast<size_t>(channels) != 0) {
            return false;
        }
        if (speaker_muted_) {
            return true;
        }
        const size_t frame_count = samples.size() / static_cast<size_t>(channels);
        stats_received_pcm_samples_.fetch_add(frame_count, std::memory_order_relaxed);
        if (channels == 1) {
            stats_playout_dropped_.fetch_add(
                playout_queue_.Write(samples.first(frame_count)),
                std::memory_order_relaxed);
            return true;
        }
        std::array<int16_t, kTenMsSamples> mono{};
        size_t offset = 0;
        while (offset < frame_count) {
            const size_t chunk = std::min(mono.size(), frame_count - offset);
            for (size_t frame = 0; frame < chunk; ++frame) {
                int32_t mixed = 0;
                for (int channel = 0; channel < channels; ++channel) {
                    mixed += samples[(offset + frame) * channels + channel];
                }
                mono[frame] = static_cast<int16_t>(mixed / channels);
            }
            stats_playout_dropped_.fetch_add(
                playout_queue_.Write(
                    std::span<const int16_t>(mono).first(chunk)),
                std::memory_order_relaxed);
            offset += chunk;
        }
        return true;
    }

    void SetMicrophoneMuted(bool muted) {
        const bool changed = microphone_muted_.exchange(muted) != muted;
        if (changed) {
            capture_queue_.Clear();
        }
    }

    void SetSpeakerMuted(bool muted) {
        const bool changed = speaker_muted_.exchange(muted) != muted;
        if (changed && muted) {
            playout_queue_.Clear();
            reverse_queue_.Clear();
        }
    }

    [[nodiscard]] bool IsRunning() const { return running_; }

    [[nodiscard]] VoiceAudioBackendInfo BackendInfo() const {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        return backend_ ? backend_->Info() : VoiceAudioBackendInfo{};
    }

    [[nodiscard]] VoiceAudioStats Stats() const {
        VoiceJitterStats jitter;
        {
            std::scoped_lock lock(jitter_mutex_);
            jitter = jitter_buffer_.Stats();
        }
        const auto apm = audio_processing_.Stats();
        return VoiceAudioStats{
            .captured_frames = stats_captured_frames_.load(std::memory_order_relaxed),
            .encoded_packets = stats_encoded_packets_.load(std::memory_order_relaxed),
            .decoded_packets = stats_decoded_packets_.load(std::memory_order_relaxed),
            .received_pcm_samples = stats_received_pcm_samples_.load(std::memory_order_relaxed),
            .capture_samples_dropped = stats_capture_dropped_.load(std::memory_order_relaxed),
            .playout_samples_dropped = stats_playout_dropped_.load(std::memory_order_relaxed),
            .playout_underruns = stats_playout_underruns_.load(std::memory_order_relaxed),
            .plc_packets = stats_plc_packets_.load(std::memory_order_relaxed),
            .jitter_duplicates = jitter.duplicates,
            .jitter_late = jitter.late,
            .jitter_invalid = jitter.invalid,
            .jitter_overflow_drops = jitter.overflow_drops,
            .jitter_missing = jitter.missing,
            .apm_capture_frames = apm.capture_frames,
            .apm_render_frames = apm.render_frames,
            .apm_capture_failures = apm.capture_failures,
            .apm_render_failures = apm.render_failures,
            .jitter_queued_packets = jitter.queued,
            .jitter_peak_packets = jitter.peak_queued,
            .device_rebuilds = stats_device_rebuilds_.load(std::memory_order_relaxed),
            .device_failures = stats_device_failures_.load(std::memory_order_relaxed),
        };
    }

private:
    static void SetError(std::string& error, std::string value) {
        error = std::move(value);
    }

    void OnCapture(std::span<const int16_t> samples) {
        if (!running_ || samples.empty()) {
            return;
        }
        const uint64_t dropped = microphone_muted_
            ? capture_queue_.WriteSilence(samples.size())
            : capture_queue_.Write(samples);
        stats_capture_dropped_.fetch_add(dropped, std::memory_order_relaxed);
        capture_cv_.notify_one();
    }

    void OnPlayout(std::span<int16_t> samples) {
        if (samples.empty()) {
            return;
        }
        std::fill(samples.begin(), samples.end(), 0);
        if (running_ && !speaker_muted_) {
            const auto read = playout_queue_.Read(samples);
            if (read < samples.size()) {
                stats_playout_underruns_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (running_) {
            // The buffer now contains exactly what is sent to the output
            // device, including local mute and underrun silence.
            reverse_queue_.Write(samples);
            capture_cv_.notify_one();
        }
    }

    void OnBackendEvent(VoiceAudioBackendEvent event, const std::string&) {
        if (!running_) {
            return;
        }
        if (event == VoiceAudioBackendEvent::kRerouted ||
            event == VoiceAudioBackendEvent::kInterruptionEnded) {
            capture_queue_.Clear();
            reverse_queue_.Clear();
            playout_queue_.Clear();
            apm_reinitialize_requested_ = true;
            stats_device_rebuilds_.fetch_add(1, std::memory_order_relaxed);
            capture_cv_.notify_one();
        }
        else if (event == VoiceAudioBackendEvent::kInterruptionBegan) {
            capture_queue_.Clear();
            reverse_queue_.Clear();
            playout_queue_.Clear();
        }
        else if (event == VoiceAudioBackendEvent::kStopped) {
            stats_device_failures_.fetch_add(1, std::memory_order_relaxed);
            SignalFatalError("device_lost");
        }
    }

    void SignalFatalError(const std::string& reason) {
        if (!fatal_error_signaled_.exchange(true)) {
            const auto callback = fatal_error_callback_;
            if (callback) {
                callback(reason);
            }
        }
    }

    void EncodeWorker() {
        std::array<int16_t, kTenMsSamples> capture{};
        std::array<int16_t, kTenMsSamples> render{};
        std::array<int16_t, kFrameSamples> encoded_frame{};
        size_t encoded_offset = 0;
        const auto weak_self = weak_from_this();

        while (running_) {
            if (apm_reinitialize_requested_.exchange(false)) {
                if (!audio_processing_.Initialize({})) {
                    stats_device_failures_.fetch_add(1, std::memory_order_relaxed);
                    SignalFatalError("audio_processing_reinitialize_failed");
                }
            }
            if (capture_queue_.Size() < capture.size()) {
                std::unique_lock lock(capture_wait_mutex_);
                capture_cv_.wait_for(lock, std::chrono::milliseconds(10),
                    [weak_self, &capture] {
                    const auto self = weak_self.lock();
                    return !self || !self->running_ ||
                        self->capture_queue_.Size() >= capture.size();
                });
                if (!running_) {
                    break;
                }
            }
            if (capture_queue_.Read(std::span<int16_t>(capture)) != capture.size()) {
                continue;
            }

            while (reverse_queue_.Size() >= render.size()) {
                if (reverse_queue_.Read(std::span<int16_t>(render)) != render.size()) {
                    break;
                }
                audio_processing_.ProcessRender(
                    std::span<const int16_t>(render));
            }
            if (!audio_processing_.ProcessCapture(std::span<int16_t>(capture))) {
                capture.fill(0);
            }
            const auto processed_capture_callback =
                processed_capture_callback_;
            if (processed_capture_callback) {
                processed_capture_callback(
                    std::span<const int16_t>(capture));
            }
            if (!running_) {
                break;
            }
            std::copy(capture.begin(), capture.end(), encoded_frame.begin() + encoded_offset);
            encoded_offset += capture.size();
            if (encoded_offset < encoded_frame.size()) {
                continue;
            }
            encoded_offset = 0;

            stats_captured_frames_.fetch_add(1, std::memory_order_relaxed);
            auto packets = encoder_->Encode(
                reinterpret_cast<const char*>(encoded_frame.data()),
                static_cast<int>(encoded_frame.size() * sizeof(int16_t)), kFrameSamples);
            for (const auto& packet : packets) {
                if (!running_ || packet.empty()) {
                    continue;
                }
                stats_encoded_packets_.fetch_add(1, std::memory_order_relaxed);
                const auto callback = callback_;
                if (callback) {
                    callback(next_sequence_++, MonotonicMillis(), packet);
                }
            }
        }
    }

    void DecodeWorker() {
        auto next_tick = std::chrono::steady_clock::now();
        const auto weak_self = weak_from_this();
        while (running_) {
            next_tick += std::chrono::milliseconds(kFrameMs);
            {
                std::unique_lock lock(jitter_wait_mutex_);
                jitter_cv_.wait_until(lock, next_tick, [weak_self] {
                    const auto self = weak_self.lock();
                    return !self || !self->running_;
                });
            }
            if (!running_) {
                break;
            }

            VoiceJitterPopResult next;
            {
                std::scoped_lock lock(jitter_mutex_);
                next = jitter_buffer_.Pop(MonotonicMillis());
            }
            if (next.kind == VoiceJitterPopKind::kNotReady) {
                continue;
            }

            std::vector<int16_t> pcm;
            if (next.kind == VoiceJitterPopKind::kPacket && next.packet) {
                pcm = decoder_->Decode(next.packet->opus, kFrameSamples, false);
                if (!pcm.empty()) {
                    stats_decoded_packets_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            else {
                pcm = decoder_->DecodeDummy(kFrameSamples);
                stats_plc_packets_.fetch_add(1, std::memory_order_relaxed);
            }
            if (!pcm.empty() && !speaker_muted_) {
                stats_playout_dropped_.fetch_add(
                    playout_queue_.Write(std::span<const int16_t>(pcm)),
                    std::memory_order_relaxed);
            }

            const auto now = std::chrono::steady_clock::now();
            if (now > next_tick + std::chrono::milliseconds(kFrameMs * 2)) {
                next_tick = now;
            }
        }
    }

    void ResetProcessing() {
        encoder_.reset();
        decoder_.reset();
        audio_processing_.Reset();
        std::scoped_lock lock(jitter_mutex_);
        jitter_buffer_.Reset();
    }

    void ResetStats() {
        stats_captured_frames_ = 0;
        stats_encoded_packets_ = 0;
        stats_decoded_packets_ = 0;
        stats_received_pcm_samples_ = 0;
        stats_capture_dropped_ = 0;
        stats_playout_dropped_ = 0;
        stats_playout_underruns_ = 0;
        stats_plc_packets_ = 0;
        stats_device_rebuilds_ = 0;
        stats_device_failures_ = 0;
        apm_reinitialize_requested_ = false;
        std::scoped_lock lock(jitter_mutex_);
        jitter_buffer_.Reset();
    }

    BoundedPcmQueue capture_queue_;
    BoundedPcmQueue reverse_queue_;
    BoundedPcmQueue playout_queue_;
    VoiceJitterBuffer jitter_buffer_;
    VoiceAudioProcessing audio_processing_;
    std::unique_ptr<OpusAudioEncoder> encoder_;
    std::unique_ptr<OpusAudioDecoder> decoder_;
    std::unique_ptr<IVoiceAudioBackend> backend_;
    BackendFactory backend_factory_;
    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex jitter_mutex_;
    PacketCallback callback_;
    FatalErrorCallback fatal_error_callback_;
    ProcessedCaptureCallback processed_capture_callback_;
    std::thread encode_worker_;
    std::thread decode_worker_;
    std::mutex capture_wait_mutex_;
    std::condition_variable capture_cv_;
    std::mutex jitter_wait_mutex_;
    std::condition_variable jitter_cv_;
    std::atomic_bool running_ = false;
    std::atomic_bool microphone_muted_ = false;
    std::atomic_bool speaker_muted_ = false;
    std::atomic_bool apm_reinitialize_requested_ = false;
    std::atomic_bool fatal_error_signaled_ = false;
    uint32_t next_sequence_ = 0;
    std::atomic_uint64_t stats_captured_frames_ = 0;
    std::atomic_uint64_t stats_encoded_packets_ = 0;
    std::atomic_uint64_t stats_decoded_packets_ = 0;
    std::atomic_uint64_t stats_received_pcm_samples_ = 0;
    std::atomic_uint64_t stats_capture_dropped_ = 0;
    std::atomic_uint64_t stats_playout_dropped_ = 0;
    std::atomic_uint64_t stats_playout_underruns_ = 0;
    std::atomic_uint64_t stats_plc_packets_ = 0;
    std::atomic_uint64_t stats_device_rebuilds_ = 0;
    std::atomic_uint64_t stats_device_failures_ = 0;
};

VoiceAudioEndpoint::VoiceAudioEndpoint(BackendFactory backend_factory)
    : impl_(std::make_shared<Impl>(std::move(backend_factory))) {}
VoiceAudioEndpoint::~VoiceAudioEndpoint() {
    if (impl_) {
        impl_->Stop();
    }
}

bool VoiceAudioEndpoint::Start(
    PacketCallback callback, std::string& error,
    FatalErrorCallback fatal_error_callback,
    ProcessedCaptureCallback processed_capture_callback) {
    return Start(
        std::move(callback), VoiceAudioBackendConfig{}, error,
        std::move(fatal_error_callback), std::move(processed_capture_callback));
}

bool VoiceAudioEndpoint::Start(
    PacketCallback callback, const VoiceAudioBackendConfig& backend_config,
    std::string& error, FatalErrorCallback fatal_error_callback,
    ProcessedCaptureCallback processed_capture_callback) {
    return impl_->Start(
        std::move(callback), backend_config, error,
        std::move(fatal_error_callback), std::move(processed_capture_callback));
}

void VoiceAudioEndpoint::Stop() { impl_->Stop(); }
bool VoiceAudioEndpoint::ReceiveOpus(
    uint32_t sequence, uint64_t capture_time_ms,
    std::span<const uint8_t> data) {
    return impl_->ReceiveOpus(sequence, capture_time_ms, data);
}
bool VoiceAudioEndpoint::ReceivePcm(
    std::span<const int16_t> samples,
    int sample_rate, int channels) {
    return impl_->ReceivePcm(samples, sample_rate, channels);
}
void VoiceAudioEndpoint::SetMicrophoneMuted(bool muted) {
    impl_->SetMicrophoneMuted(muted);
}
void VoiceAudioEndpoint::SetSpeakerMuted(bool muted) { impl_->SetSpeakerMuted(muted); }
bool VoiceAudioEndpoint::IsRunning() const { return impl_->IsRunning(); }
VoiceAudioStats VoiceAudioEndpoint::Stats() const { return impl_->Stats(); }
VoiceAudioBackendInfo VoiceAudioEndpoint::BackendInfo() const {
    return impl_->BackendInfo();
}

}  // namespace px
