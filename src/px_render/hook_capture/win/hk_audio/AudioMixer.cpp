#include "AudioMixer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "AudioShare.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"

namespace px {
namespace {

constexpr size_t kMaxQueueBytes = 16 * 1024 * 1024;  // byte-based, not packet count
constexpr size_t kFlushFrames = 48000 / 50;  // 20ms @ 48k

int16_t ClampS16(float v) {
    if (v > 32767.f) {
        return 32767;
    }
    if (v < -32768.f) {
        return -32768;
    }
    return static_cast<int16_t>(v);
}

}  // namespace

AudioMixer::AudioMixer(std::shared_ptr<AudioShare> share) : share_(std::move(share)) {
    if (share_) {
        share_->SetAudioFormat(SimpleAudioFormat::kPCM_S16, kOutRate, kOutChannels, 16);
    }
    worker_ = std::thread([this] { WorkerMain(); });
}

AudioMixer::~AudioMixer() {
    stop_.store(true, std::memory_order_release);
    q_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::vector<int16_t> AudioMixer::ToS16(const void* data,
                                       int bytes,
                                       SimpleAudioFormat format,
                                       int channels) {
    std::vector<int16_t> out;
    if (!data || bytes <= 0 || channels <= 0) {
        return out;
    }
    if (format == SimpleAudioFormat::kPCM_F32) {
        const int n = bytes / static_cast<int>(sizeof(float));
        const auto* f = static_cast<const float*>(data);
        out.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; i++) {
            float v = f[i];
            if (v > 1.f) {
                v = 1.f;
            } else if (v < -1.f) {
                v = -1.f;
            }
            out[static_cast<size_t>(i)] = static_cast<int16_t>(v * 32767.f);
        }
        return out;
    }
    if (format != SimpleAudioFormat::kPCM_S16) {
        // Unsupported format must never be reinterpreted as s16.
        static std::atomic<uint64_t> s_n{0};
        const auto n = s_n.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1 || (n % 200) == 0) {
            LOGW("AudioMixer: drop packet, unsupported format={} n={}", static_cast<int>(format), n);
        }
        return out;
    }
    const int n = bytes / static_cast<int>(sizeof(int16_t));
    out.resize(static_cast<size_t>(n));
    std::memcpy(out.data(), data, static_cast<size_t>(n) * sizeof(int16_t));
    return out;
}

std::vector<int16_t> AudioMixer::ResampleStereo(const std::vector<int16_t>& in,
                                                int in_rate,
                                                int in_ch,
                                                int out_rate) {
    if (in.empty() || in_rate <= 0 || in_ch <= 0 || out_rate <= 0) {
        return {};
    }
    const size_t in_frames = in.size() / static_cast<size_t>(in_ch);
    if (in_frames == 0) {
        return {};
    }

    // Down/up-mix to stereo first.
    std::vector<int16_t> stereo(in_frames * 2);
    for (size_t i = 0; i < in_frames; i++) {
        if (in_ch == 1) {
            stereo[i * 2] = in[i];
            stereo[i * 2 + 1] = in[i];
        } else {
            stereo[i * 2] = in[i * static_cast<size_t>(in_ch)];
            stereo[i * 2 + 1] = in[i * static_cast<size_t>(in_ch) + 1];
        }
    }

    if (in_rate == out_rate) {
        return stereo;
    }

    const size_t out_frames =
        static_cast<size_t>((static_cast<double>(in_frames) * out_rate) / in_rate);
    std::vector<int16_t> out(out_frames * 2);
    for (size_t i = 0; i < out_frames; i++) {
        const double src = static_cast<double>(i) * in_rate / out_rate;
        size_t i0 = static_cast<size_t>(src);
        if (i0 >= in_frames) {
            i0 = in_frames - 1;
        }
        size_t i1 = i0 + 1;
        if (i1 >= in_frames) {
            i1 = in_frames - 1;
        }
        const float t = static_cast<float>(src - static_cast<double>(i0));
        for (int c = 0; c < 2; c++) {
            const float a = stereo[i0 * 2 + c];
            const float b = stereo[i1 * 2 + c];
            out[i * 2 + c] = ClampS16(a + (b - a) * t);
        }
    }
    return out;
}

void AudioMixer::Push(const void* data,
                      int bytes,
                      SimpleAudioFormat format,
                      int sample_rate,
                      int channels,
                      const char* source_tag) {
    if (!data || bytes <= 0 || stop_.load(std::memory_order_acquire)) {
        return;
    }
    Packet pkt;
    pkt.s16 = ToS16(data, bytes, format, channels);
    if (pkt.s16.empty()) {
        return;
    }
    pkt.sample_rate = sample_rate > 0 ? sample_rate : kOutRate;
    pkt.channels = channels > 0 ? channels : 2;
    pkt.tag = source_tag ? source_tag : "?";

    const size_t pkt_bytes = pkt.s16.size() * sizeof(int16_t);
    uint64_t dropped = 0;
    {
        std::lock_guard lock(q_mu_);
        // Byte-based cap: drop oldest until the new packet fits.
        while (q_bytes_ + pkt_bytes > kMaxQueueBytes && !q_.empty()) {
            q_bytes_ -= q_.front().s16.size() * sizeof(int16_t);
            q_.pop();
            dropped = dropped_.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        q_bytes_ += pkt_bytes;
        q_.push(std::move(pkt));
    }
    if (dropped && (dropped == 1 || (dropped % 100) == 0)) {
        LOGW("AudioMixer: queue over {} bytes, dropped oldest n={}", kMaxQueueBytes, dropped);
    }
    pushed_.fetch_add(1, std::memory_order_relaxed);
    q_cv_.notify_one();
}

void AudioMixer::WorkerMain() {
    while (true) {
        Packet pkt;
        {
            std::unique_lock lock(q_mu_);
            q_cv_.wait(lock, [&] { return stop_.load(std::memory_order_acquire) || !q_.empty(); });
            if (q_.empty()) {
                if (stop_.load(std::memory_order_acquire)) {
                    break;
                }
                continue;
            }
            pkt = std::move(q_.front());
            q_bytes_ -= pkt.s16.size() * sizeof(int16_t);
            q_.pop();
        }

        auto stereo = ResampleStereo(pkt.s16, pkt.sample_rate, pkt.channels, kOutRate);
        if (!share_) {
            static std::atomic<uint64_t> s_n{0};
            if (++s_n == 1 || (s_n.load() % 200) == 0) {
                LOGE("AudioMixer: share_ null, drop tag={} n={}", pkt.tag, s_n.load());
            }
            continue;
        }
        if (stereo.empty()) {
            static std::atomic<uint64_t> s_n{0};
            if (++s_n == 1 || (s_n.load() % 200) == 0) {
                LOGE("AudioMixer: resample empty tag={} {}Hz {}ch n={}", pkt.tag, pkt.sample_rate,
                     pkt.channels, s_n.load());
            }
            continue;
        }

        const size_t frames = stereo.size() / 2;
        if (acc_.size() < (acc_frames_ + frames) * 2) {
            acc_.resize((acc_frames_ + frames) * 2 + kFlushFrames * 2, 0.f);
        }
        for (size_t i = 0; i < frames; i++) {
            acc_[(acc_frames_ + i) * 2] += stereo[i * 2];
            acc_[(acc_frames_ + i) * 2 + 1] += stereo[i * 2 + 1];
        }
        acc_frames_ += frames;

        while (acc_frames_ >= kFlushFrames) {
            std::vector<int16_t> chunk(kFlushFrames * 2);
            for (size_t i = 0; i < kFlushFrames * 2; i++) {
                chunk[i] = ClampS16(acc_[i]);
            }
            // Shift remaining accumulator.
            const size_t remain = acc_frames_ - kFlushFrames;
            if (remain > 0) {
                std::memmove(acc_.data(), acc_.data() + kFlushFrames * 2,
                             remain * 2 * sizeof(float));
            }
            std::fill(acc_.begin() + remain * 2, acc_.begin() + (remain + kFlushFrames) * 2, 0.f);
            acc_frames_ = remain;

            share_->SetAudioFormat(SimpleAudioFormat::kPCM_S16, kOutRate, kOutChannels, 16);
            share_->PostAudioData(Data::Make(reinterpret_cast<const char*>(chunk.data()),
                                             static_cast<int>(chunk.size() * sizeof(int16_t))));
            const auto n = mixed_.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || (n % 100) == 0) {
                LOGI("AudioMixer: flushed blocks={} last_src={} pushed={}", n, pkt.tag,
                     pushed_.load());
            }
        }
    }
}

}  // namespace px
