#include "opus_encoder_runtime.h"

#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "px_common/data.h"
#include "px_common/async_runtime.h"
#include "px_common/file.h"
#include "px_common/log.h"
#include "px_common/string_util.h"
#include "px_opus_codec/opus_codec.h"

namespace px {

struct OpusEncoderRuntime::WorkerState final {
    std::mutex mutex;
    std::condition_variable_any condition;
    std::deque<Entry> queue;
    Config config;
    std::shared_ptr<DeliveryChannel> delivery_channel;
    std::shared_ptr<OpusAudioEncoder> encoder;
    std::shared_ptr<OpusAudioDecoder> decoder;
    std::shared_ptr<File> original_pcm_file;
    std::shared_ptr<File> decoded_pcm_file;
    std::vector<char> audio_cache;
    int callback_count = 0;
    int sample_rate = 0;
    int channels = 0;
    int bits = 0;
    bool shutting_down = false;
};

void OpusEncoderRuntime::DeliveryChannel::Set(EncodedDelivery callback) {
    std::lock_guard lock(mutex);
    if (accepting) {
        delivery = std::move(callback);
    }
}

void OpusEncoderRuntime::DeliveryChannel::Clear() {
    std::lock_guard lock(mutex);
    delivery = {};
}

void OpusEncoderRuntime::DeliveryChannel::Disable() {
    std::lock_guard lock(mutex);
    accepting = false;
    delivery = {};
}

void OpusEncoderRuntime::DeliveryChannel::Deliver(const std::shared_ptr<Data>& data, int sample_rate, int channels, int bits, int frame_size) {
    EncodedDelivery callback;
    {
        std::lock_guard lock(mutex);
        if (!accepting) {
            return;
        }
        callback = delivery;
    }
    if (callback) {
        callback(data, sample_rate, channels, bits, frame_size);
    }
}

std::shared_ptr<OpusEncoderRuntime> OpusEncoderRuntime::Make(Config config) {
    auto channel = std::make_shared<DeliveryChannel>();
    auto state = std::make_shared<WorkerState>();
    state->config = config;
    state->delivery_channel = channel;
    auto runtime = std::make_shared<OpusEncoderRuntime>(ConstructionToken{}, std::move(config), std::move(channel), std::move(state));
    runtime->StartWorker();
    return runtime;
}

OpusEncoderRuntime::OpusEncoderRuntime(ConstructionToken, Config config, std::shared_ptr<DeliveryChannel> delivery_channel,
                                       std::shared_ptr<WorkerState> worker_state)
    : config_(std::move(config)), delivery_channel_(std::move(delivery_channel)), worker_state_(std::move(worker_state)) {}

OpusEncoderRuntime::~OpusEncoderRuntime() {
    Shutdown();
    std::jthread deferred_worker{};
    {
        std::lock_guard shutdown_lock(shutdown_mutex_);
        if (worker_.joinable() && worker_.get_id() == std::this_thread::get_id()) {
            deferred_worker = std::move(worker_);
        }
    }
    PxAsyncRuntime::DeferJoin(std::move(deferred_worker));
}

void OpusEncoderRuntime::StartWorker() {
    const auto state = worker_state_;
    worker_ = std::jthread([state](std::stop_token stop_token) { WorkerMain(state, stop_token); });
}

void OpusEncoderRuntime::SetDelivery(EncodedDelivery delivery) {
    delivery_channel_->Set(std::move(delivery));
}

void OpusEncoderRuntime::ClearDelivery() {
    delivery_channel_->Clear();
}

void OpusEncoderRuntime::Enqueue(const std::shared_ptr<Data>& data, int sample_rate, int channels, int bits) {
    if (!accepting_.load() || !data || data->Size() <= 0) {
        return;
    }
    {
        std::lock_guard lock(worker_state_->mutex);
        if (!accepting_.load() || worker_state_->shutting_down) {
            return;
        }
        if (worker_state_->queue.size() >= kMaxQueue) {
            ++dropped_;
            return;
        }
        worker_state_->queue.push_back(Entry{
            .data = data,
            .sample_rate = sample_rate,
            .channels = channels,
            .bits = bits,
        });
    }
    worker_state_->condition.notify_one();
}

bool OpusEncoderRuntime::IsAccepting() const {
    return accepting_.load();
}

uint64_t OpusEncoderRuntime::DroppedCount() const {
    return dropped_.load();
}

void OpusEncoderRuntime::Shutdown() {
    std::jthread worker_to_join{};
    {
        std::lock_guard shutdown_lock(shutdown_mutex_);
        if (accepting_.exchange(false)) {
            delivery_channel_->Disable();
            {
                std::lock_guard state_lock(worker_state_->mutex);
                worker_state_->shutting_down = true;
            }
            worker_state_->condition.notify_all();
            worker_.request_stop();
        }
        if (!worker_.joinable() || worker_.get_id() == std::this_thread::get_id()) {
            return;
        }
        worker_to_join = std::move(worker_);
    }
    if (worker_to_join.joinable()) {
        worker_to_join.join();
    }
}

void OpusEncoderRuntime::WorkerMain(const std::shared_ptr<WorkerState>& state, std::stop_token stop_token) {
    while (true) {
        Entry entry;
        {
            std::unique_lock lock(state->mutex);
            state->condition.wait(lock, stop_token, [state] { return state->shutting_down || !state->queue.empty(); });
            if (state->queue.empty()) {
                if (state->shutting_down || stop_token.stop_requested()) {
                    break;
                }
                continue;
            }
            entry = std::move(state->queue.front());
            state->queue.pop_front();
        }
        try {
            ProcessEntry(state, entry);
        } catch (const std::exception& error) {
            LOGE("OpusEncoder worker exception: {}", error.what());
        } catch (...) {
            LOGE("OpusEncoder worker exception: unknown exception");
        }
    }
    state->audio_cache.clear();
    state->encoder.reset();
    state->decoder.reset();
    state->original_pcm_file.reset();
    state->decoded_pcm_file.reset();
}

void OpusEncoderRuntime::ProcessEntry(const std::shared_ptr<WorkerState>& state, const Entry& entry) {
    if (!entry.data || entry.data->Size() <= 0 || entry.sample_rate <= 0 || entry.channels <= 0 || entry.bits <= 0) {
        return;
    }
    const bool format_changed = state->sample_rate != entry.sample_rate || state->channels != entry.channels || state->bits != entry.bits;
    if (!state->encoder || format_changed) {
        state->audio_cache.clear();
        state->callback_count = 0;
        state->decoder.reset();
        state->encoder = std::make_shared<OpusAudioEncoder>(entry.sample_rate, entry.channels, entry.bits, OPUS_APPLICATION_AUDIO, 15);
        if (!state->encoder->valid()) {
            state->encoder.reset();
            return;
        }
        state->encoder->SetComplexity(8);
        state->sample_rate = entry.sample_rate;
        state->channels = entry.channels;
        state->bits = entry.bits;
        LOGI("audio format, samples: {}, channels: {}, bits: {}", entry.sample_rate, entry.channels, entry.bits);
    }

    if (state->config.debug_decoder) {
        if (!state->original_pcm_file) {
            state->original_pcm_file = File::OpenForWriteB(PathFromUTF8("1.opus.encoder.plugin.origin.pcm"));
        }
        if (state->original_pcm_file) {
            state->original_pcm_file->Append(entry.data);
        }
    }

    const auto input = entry.data->AsString();
    state->audio_cache.insert(state->audio_cache.end(), input.begin(), input.end());
    if (++state->callback_count < 2) {
        return;
    }
    const int bytes_per_sample = entry.bits / 8;
    if (bytes_per_sample <= 0 || entry.channels <= 0) {
        state->audio_cache.clear();
        state->callback_count = 0;
        return;
    }
    const auto bytes_per_frame = static_cast<size_t>(bytes_per_sample) * static_cast<size_t>(entry.channels);
    const int frame_size = static_cast<int>(state->audio_cache.size() / bytes_per_frame);
    const auto encoded_frames = state->encoder->Encode(std::as_bytes(std::span{state->audio_cache}), frame_size);
    for (const auto& encoded_frame : encoded_frames) {
        auto encoded_data = Data::Copy(std::span<const char>{reinterpret_cast<const char*>(encoded_frame.data()), encoded_frame.size()});
        state->delivery_channel->Deliver(encoded_data, entry.sample_rate, entry.channels, entry.bits, frame_size);

        if (state->config.debug_decoder) {
            if (!state->decoder) {
                state->decoder = std::make_shared<OpusAudioDecoder>(state->encoder->SampleRate(), state->encoder->Channels());
            }
            const auto pcm = state->decoder->Decode(encoded_frame, frame_size, false);
            if (!state->decoded_pcm_file) {
                state->decoded_pcm_file = File::OpenForWriteB(PathFromUTF8("1.test.pcm"));
            }
            if (state->decoded_pcm_file && !pcm.empty()) {
                state->decoded_pcm_file->Append(
                    Data::Copy(std::span<const char>{reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(pcm.front())}));
            }
        }
    }
    state->audio_cache.clear();
    state->callback_count = 0;
}

} // namespace px
