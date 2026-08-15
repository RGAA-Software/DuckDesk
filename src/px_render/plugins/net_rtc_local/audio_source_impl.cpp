#include "audio_source_impl.h"

#include "px_common_new/log.h"

namespace tc
{

    void AudioSourceImpl::SendAudio(const void* data,
                                    size_t size_bytes,
                                    int sample_rate,
                                    int channels,
                                    int bits_per_sample) {
        if (!data || size_bytes == 0 || sample_rate <= 0 || channels <= 0 || bits_per_sample <= 0) {
            return;
        }
        if ((bits_per_sample % 8) != 0) {
            return;
        }

        const int bytes_per_sample = bits_per_sample / 8;
        const int bytes_per_frame = bytes_per_sample * channels;
        if (bytes_per_frame <= 0) {
            return;
        }

        // WebRTC audio path expects 10ms packets.
        const int frames_per_10ms = sample_rate / 100;
        if (frames_per_10ms <= 0) {
            return;
        }
        const size_t bytes_per_10ms = static_cast<size_t>(frames_per_10ms) * static_cast<size_t>(bytes_per_frame);

        {
            const auto* src = static_cast<const uint8_t*>(data);
            pending_.insert(pending_.end(), src, src + size_bytes);
        }

        std::lock_guard<std::mutex> lock(sink_lock_);
        if (sinks_.empty()) {
            // Keep a small pending window so we don't grow forever while no track sink is attached.
            if (pending_.size() > bytes_per_10ms * 10) {
                pending_.erase(pending_.begin(), pending_.end() - static_cast<std::ptrdiff_t>(bytes_per_10ms));
            }
            return;
        }

        size_t offset = 0;
        while (pending_.size() - offset >= bytes_per_10ms) {
            const uint8_t* chunk = pending_.data() + offset;
            for (auto* sink : sinks_) {
                if (!sink) {
                    continue;
                }
                sink->OnData(chunk, bits_per_sample, sample_rate,
                             static_cast<size_t>(channels),
                             static_cast<size_t>(frames_per_10ms));
            }
            offset += bytes_per_10ms;
            ++sent_10ms_chunks_;
            if (sent_10ms_chunks_ == 1 || sent_10ms_chunks_ % 500 == 0) {
                LOGI("[AudioSourceImpl] SendAudio #{} 10ms chunks, rate={} ch={} bits={}",
                     sent_10ms_chunks_, sample_rate, channels, bits_per_sample);
            }
        }

        if (offset > 0) {
            pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }

} // namespace tc
