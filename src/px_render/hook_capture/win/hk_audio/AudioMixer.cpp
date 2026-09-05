#include "AudioMixer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>

#include "AudioShare.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/thread.h"

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

class AudioMixer::State {
public:
    explicit State(std::shared_ptr<AudioShare> audio_share) : share(std::move(audio_share)) {}

    std::shared_ptr<AudioShare> share;
    std::mutex q_mu;
    std::condition_variable q_cv;
    std::queue<Packet> q;
    size_t q_bytes = 0;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> pushed{0};
    std::atomic<uint64_t> mixed{0};
    std::atomic<uint64_t> dropped{0};
    std::vector<float> accumulator;
    size_t accumulator_frames = 0;
};

AudioMixer::AudioMixer(std::shared_ptr<AudioShare> share)
    : state_(std::make_shared<State>(std::move(share))) {
    if (state_->share) {
        state_->share->SetAudioFormat(SimpleAudioFormat::kPCM_S16, kOutRate, kOutChannels, 16);
    }
    const auto state = state_;
    worker_ = Thread::MakeOnceTask([state]() { WorkerMain(state); }, "hook_audio_mixer", false);
}

AudioMixer::~AudioMixer() {
    Stop();
}

void AudioMixer::Stop() {
    const auto state = state_;
    if (!state) {
        return;
    }
    state->stop.store(true, std::memory_order_release);
    state->q_cv.notify_all();
    const auto worker = std::move(worker_);
    if (worker) {
        worker->Exit();
    }
}

uint64_t AudioMixer::pushed() const {
    const auto state = state_;
    return state ? state->pushed.load(std::memory_order_relaxed) : 0;
}

uint64_t AudioMixer::mixed() const {
    const auto state = state_;
    return state ? state->mixed.load(std::memory_order_relaxed) : 0;
}

std::vector<int16_t> AudioMixer::ToS16(std::span<const std::byte> data,
                                       SimpleAudioFormat format,
                                       int channels) {
    std::vector<int16_t> out;
    if (data.empty() || channels <= 0) {
        return out;
    }
    if (format == SimpleAudioFormat::kPCM_F32) {
        const size_t n = data.size_bytes() / sizeof(float);
        const auto samples = std::span(reinterpret_cast<const float*>(data.data()), n);
        out.resize(static_cast<size_t>(n));
        for (size_t i = 0; i < n; i++) {
            float v = samples[i];
            if (v > 1.f) {
                v = 1.f;
            } else if (v < -1.f) {
                v = -1.f;
            }
            out[i] = static_cast<int16_t>(v * 32767.f);
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
    const size_t n = data.size_bytes() / sizeof(int16_t);
    out.resize(n);
    std::memcpy(out.data(), data.data(), std::span(out).size_bytes());
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

void AudioMixer::Push(std::span<const std::byte> data,
                      SimpleAudioFormat format,
                      int sample_rate,
                      int channels,
                      std::string_view source_tag) {
    const auto state = state_;
    if (!state || data.empty() || state->stop.load(std::memory_order_acquire)) {
        return;
    }
    Packet pkt;
    pkt.s16 = ToS16(data, format, channels);
    if (pkt.s16.empty()) {
        return;
    }
    pkt.sample_rate = sample_rate > 0 ? sample_rate : kOutRate;
    pkt.channels = channels > 0 ? channels : 2;
    pkt.tag = source_tag.empty() ? "?" : std::string(source_tag);

    const size_t pkt_bytes = pkt.s16.size() * sizeof(int16_t);
    uint64_t dropped = 0;
    {
        std::lock_guard lock(state->q_mu);
        if (state->stop.load(std::memory_order_acquire)) {
            return;
        }
        // Byte-based cap: drop oldest until the new packet fits.
        while (state->q_bytes + pkt_bytes > kMaxQueueBytes && !state->q.empty()) {
            state->q_bytes -= state->q.front().s16.size() * sizeof(int16_t);
            state->q.pop();
            dropped = state->dropped.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        state->q_bytes += pkt_bytes;
        state->q.push(std::move(pkt));
    }
    if (dropped && (dropped == 1 || (dropped % 100) == 0)) {
        LOGW("AudioMixer: queue over {} bytes, dropped oldest n={}", kMaxQueueBytes, dropped);
    }
    state->pushed.fetch_add(1, std::memory_order_relaxed);
    state->q_cv.notify_one();
}

void AudioMixer::WorkerMain(const std::shared_ptr<State>& state) {
    while (true) {
        Packet pkt;
        {
            std::unique_lock lock(state->q_mu);
            state->q_cv.wait(lock, [state] {
                return state->stop.load(std::memory_order_acquire) || !state->q.empty();
            });
            if (state->q.empty()) {
                if (state->stop.load(std::memory_order_acquire)) {
                    break;
                }
                continue;
            }
            pkt = std::move(state->q.front());
            state->q_bytes -= pkt.s16.size() * sizeof(int16_t);
            state->q.pop();
        }

        auto stereo = ResampleStereo(pkt.s16, pkt.sample_rate, pkt.channels, kOutRate);
        if (!state->share) {
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
        if (state->accumulator.size() < (state->accumulator_frames + frames) * 2) {
            state->accumulator.resize(
                (state->accumulator_frames + frames) * 2 + kFlushFrames * 2, 0.f);
        }
        for (size_t i = 0; i < frames; i++) {
            state->accumulator[(state->accumulator_frames + i) * 2] += stereo[i * 2];
            state->accumulator[(state->accumulator_frames + i) * 2 + 1] += stereo[i * 2 + 1];
        }
        state->accumulator_frames += frames;

        while (state->accumulator_frames >= kFlushFrames) {
            std::vector<int16_t> chunk(kFlushFrames * 2);
            for (size_t i = 0; i < kFlushFrames * 2; i++) {
                chunk[i] = ClampS16(state->accumulator[i]);
            }
            // Shift remaining accumulator.
            const size_t remain = state->accumulator_frames - kFlushFrames;
            if (remain > 0) {
                std::memmove(state->accumulator.data(),
                             state->accumulator.data() + kFlushFrames * 2,
                             remain * 2 * sizeof(float));
            }
            std::fill(state->accumulator.begin() + remain * 2,
                      state->accumulator.begin() + (remain + kFlushFrames) * 2, 0.f);
            state->accumulator_frames = remain;

            state->share->SetAudioFormat(
                SimpleAudioFormat::kPCM_S16, kOutRate, kOutChannels, 16);
            state->share->PostAudioData(
                Data::Copy(std::span<const char>{reinterpret_cast<const char*>(chunk.data()), chunk.size() * sizeof(std::int16_t)}));
            const auto n = state->mixed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || (n % 100) == 0) {
                LOGI("AudioMixer: flushed blocks={} last_src={} pushed={}", n, pkt.tag,
                     state->pushed.load());
            }
        }
    }
}

}  // namespace px
