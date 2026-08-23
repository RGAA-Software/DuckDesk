#include "voice_audio_endpoint.h"

#include <SDL2/SDL.h>
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

namespace px {
namespace {

uint64_t MonotonicMillis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

class BoundedPcmQueue {
public:
    explicit BoundedPcmQueue(size_t capacity_samples) : storage_(capacity_samples) {}

    uint64_t Write(const int16_t* samples, size_t count) {
        if (!samples || count == 0) {
            return 0;
        }
        std::scoped_lock lock(mutex_);
        uint64_t dropped = 0;
        if (count >= storage_.size()) {
            dropped += size_ + count - storage_.size();
            samples += count - storage_.size();
            count = storage_.size();
            read_ = write_ = size_ = 0;
        } else if (size_ + count > storage_.size()) {
            const auto remove = size_ + count - storage_.size();
            read_ = (read_ + remove) % storage_.size();
            size_ -= remove;
            dropped += remove;
        }
        for (size_t i = 0; i < count; ++i) {
            storage_[write_] = samples[i];
            write_ = (write_ + 1) % storage_.size();
        }
        size_ += count;
        return dropped;
    }

    size_t Read(int16_t* destination, size_t count) {
        if (!destination || count == 0) {
            return 0;
        }
        std::scoped_lock lock(mutex_);
        const auto available = std::min(count, size_);
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
    mutable std::mutex mutex_;
    std::vector<int16_t> storage_;
    size_t read_ = 0;
    size_t write_ = 0;
    size_t size_ = 0;
};

}  // namespace

class VoiceAudioEndpoint::Impl {
public:
    Impl()
        : capture_queue_(kFrameSamples * 5),
          playout_queue_(kFrameSamples * 10) {}

    ~Impl() { Stop(); }

    bool Start(PacketCallback callback, std::string* error) {
        Stop();
        if (!callback) {
            SetError(error, "voice packet callback is empty");
            return false;
        }
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SetError(error, std::string("SDL audio initialization failed: ") + SDL_GetError());
            return false;
        }

        encoder_ = std::make_unique<OpusAudioEncoder>(
            kSampleRate, kChannels, kBitsPerSample, OPUS_APPLICATION_VOIP, 10);
        decoder_ = std::make_unique<OpusAudioDecoder>(kSampleRate, kChannels);
        if (!encoder_->valid() || !decoder_->valid()) {
            SetError(error, "Opus voice codec initialization failed");
            encoder_.reset();
            decoder_.reset();
            return false;
        }
        encoder_->SetBitrate(kBitrateBps);
        encoder_->SetComplexity(8);
        callback_ = std::move(callback);

        SDL_AudioSpec desired{};
        desired.freq = kSampleRate;
        desired.format = AUDIO_S16SYS;
        desired.channels = kChannels;
        desired.samples = kSampleRate / 100;  // 10 ms device callback
        desired.userdata = this;
        desired.callback = &Impl::CaptureCallback;
        capture_device_ = SDL_OpenAudioDevice(nullptr, SDL_TRUE, &desired, nullptr, 0);
        if (capture_device_ == 0) {
            SetError(error, std::string("default microphone unavailable: ") + SDL_GetError());
            ResetCodec();
            return false;
        }

        desired.callback = &Impl::PlaybackCallback;
        playout_device_ = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &desired, nullptr, 0);
        if (playout_device_ == 0) {
            SetError(error, std::string("default communication output unavailable: ") + SDL_GetError());
            SDL_CloseAudioDevice(capture_device_);
            capture_device_ = 0;
            ResetCodec();
            return false;
        }

        next_sequence_ = 0;
        running_ = true;
        worker_ = std::thread([this] { EncodeWorker(); });
        SDL_PauseAudioDevice(playout_device_, 0);
        SDL_PauseAudioDevice(capture_device_, 0);
        return true;
    }

    void Stop() {
        const bool was_running = running_.exchange(false);
        if (capture_device_ != 0) {
            SDL_PauseAudioDevice(capture_device_, 1);
            SDL_CloseAudioDevice(capture_device_);
            capture_device_ = 0;
        }
        capture_cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (playout_device_ != 0) {
            SDL_PauseAudioDevice(playout_device_, 1);
            SDL_CloseAudioDevice(playout_device_);
            playout_device_ = 0;
        }
        if (was_running || encoder_ || decoder_) {
            capture_queue_.Clear();
            playout_queue_.Clear();
            ResetCodec();
        }
        callback_ = {};
    }

    bool ReceiveOpus(const void* data, size_t size) {
        if (!running_ || !data || size == 0 || speaker_muted_) {
            return false;
        }
        std::scoped_lock lock(decoder_mutex_);
        if (!decoder_) {
            return false;
        }
        const auto* begin = static_cast<const uint8_t*>(data);
        std::vector<unsigned char> packet(begin, begin + size);
        const auto pcm = decoder_->Decode(packet, kFrameSamples, false);
        if (pcm.empty()) {
            return false;
        }
        stats_decoded_packets_.fetch_add(1, std::memory_order_relaxed);
        stats_playout_dropped_.fetch_add(
            playout_queue_.Write(pcm.data(), pcm.size()), std::memory_order_relaxed);
        return true;
    }

    void SetMicrophoneMuted(bool muted) { microphone_muted_ = muted; }
    void SetSpeakerMuted(bool muted) {
        speaker_muted_ = muted;
        if (muted) {
            playout_queue_.Clear();
        }
    }
    [[nodiscard]] bool IsRunning() const { return running_; }

    [[nodiscard]] VoiceAudioStats Stats() const {
        return VoiceAudioStats{
            .captured_frames = stats_captured_frames_.load(std::memory_order_relaxed),
            .encoded_packets = stats_encoded_packets_.load(std::memory_order_relaxed),
            .decoded_packets = stats_decoded_packets_.load(std::memory_order_relaxed),
            .capture_samples_dropped = stats_capture_dropped_.load(std::memory_order_relaxed),
            .playout_samples_dropped = stats_playout_dropped_.load(std::memory_order_relaxed),
            .playout_underruns = stats_playout_underruns_.load(std::memory_order_relaxed),
        };
    }

private:
    static void SetError(std::string* error, std::string value) {
        if (error) {
            *error = std::move(value);
        }
    }

    static void CaptureCallback(void* userdata, Uint8* stream, int length) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self || !self->running_ || self->microphone_muted_ || length <= 0) {
            return;
        }
        const auto sample_count = static_cast<size_t>(length) / sizeof(int16_t);
        self->stats_capture_dropped_.fetch_add(
            self->capture_queue_.Write(reinterpret_cast<const int16_t*>(stream), sample_count),
            std::memory_order_relaxed);
        self->capture_cv_.notify_one();
    }

    static void PlaybackCallback(void* userdata, Uint8* stream, int length) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self || length <= 0) {
            return;
        }
        std::memset(stream, 0, static_cast<size_t>(length));
        if (!self->running_ || self->speaker_muted_) {
            return;
        }
        const auto wanted = static_cast<size_t>(length) / sizeof(int16_t);
        const auto read = self->playout_queue_.Read(reinterpret_cast<int16_t*>(stream), wanted);
        if (read < wanted) {
            self->stats_playout_underruns_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void EncodeWorker() {
        std::array<int16_t, kFrameSamples> pcm{};
        while (running_) {
            if (capture_queue_.Size() < pcm.size()) {
                std::unique_lock lock(capture_wait_mutex_);
                capture_cv_.wait_for(lock, std::chrono::milliseconds(20), [this, &pcm] {
                    return !running_ || capture_queue_.Size() >= pcm.size();
                });
                if (!running_) {
                    break;
                }
            }
            if (capture_queue_.Read(pcm.data(), pcm.size()) != pcm.size()) {
                continue;
            }
            stats_captured_frames_.fetch_add(1, std::memory_order_relaxed);
            auto packets = encoder_->Encode(
                reinterpret_cast<const char*>(pcm.data()),
                static_cast<int>(pcm.size() * sizeof(int16_t)), kFrameSamples);
            for (const auto& packet : packets) {
                if (!running_ || packet.empty()) {
                    continue;
                }
                stats_encoded_packets_.fetch_add(1, std::memory_order_relaxed);
                callback_(next_sequence_++, MonotonicMillis(), packet);
            }
        }
    }

    void ResetCodec() {
        std::scoped_lock lock(decoder_mutex_);
        encoder_.reset();
        decoder_.reset();
    }

    BoundedPcmQueue capture_queue_;
    BoundedPcmQueue playout_queue_;
    std::unique_ptr<OpusAudioEncoder> encoder_;
    std::unique_ptr<OpusAudioDecoder> decoder_;
    std::mutex decoder_mutex_;
    PacketCallback callback_;
    SDL_AudioDeviceID capture_device_ = 0;
    SDL_AudioDeviceID playout_device_ = 0;
    std::thread worker_;
    std::mutex capture_wait_mutex_;
    std::condition_variable capture_cv_;
    std::atomic_bool running_ = false;
    std::atomic_bool microphone_muted_ = false;
    std::atomic_bool speaker_muted_ = false;
    uint32_t next_sequence_ = 0;
    std::atomic_uint64_t stats_captured_frames_ = 0;
    std::atomic_uint64_t stats_encoded_packets_ = 0;
    std::atomic_uint64_t stats_decoded_packets_ = 0;
    std::atomic_uint64_t stats_capture_dropped_ = 0;
    std::atomic_uint64_t stats_playout_dropped_ = 0;
    std::atomic_uint64_t stats_playout_underruns_ = 0;
};

VoiceAudioEndpoint::VoiceAudioEndpoint() : impl_(std::make_unique<Impl>()) {}
VoiceAudioEndpoint::~VoiceAudioEndpoint() = default;

bool VoiceAudioEndpoint::Start(PacketCallback callback, std::string* error) {
    return impl_->Start(std::move(callback), error);
}

void VoiceAudioEndpoint::Stop() { impl_->Stop(); }
bool VoiceAudioEndpoint::ReceiveOpus(const void* data, size_t size) {
    return impl_->ReceiveOpus(data, size);
}
void VoiceAudioEndpoint::SetMicrophoneMuted(bool muted) { impl_->SetMicrophoneMuted(muted); }
void VoiceAudioEndpoint::SetSpeakerMuted(bool muted) { impl_->SetSpeakerMuted(muted); }
bool VoiceAudioEndpoint::IsRunning() const { return impl_->IsRunning(); }
VoiceAudioStats VoiceAudioEndpoint::Stats() const { return impl_->Stats(); }

}  // namespace px
