#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "SimpleAudioFormatConverter.h"

namespace px {

class AudioShare;
class Data;

// Collects PCM from multiple hook sources (WASAPI clients, XAudio2 voices,
// DirectSound buffers, waveOut) and mixes to a single 48kHz stereo s16 stream
// for AudioShare / verification WAV.
class AudioMixer {
public:
    static constexpr int kOutRate = 48000;
    static constexpr int kOutChannels = 2;

    explicit AudioMixer(std::shared_ptr<AudioShare> share);
    ~AudioMixer();

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    // Realtime-safe: enqueue only.
    void Push(const void* data,
              int bytes,
              SimpleAudioFormat format,
              int sample_rate,
              int channels,
              const char* source_tag);

    uint64_t pushed() const { return pushed_.load(std::memory_order_relaxed); }
    uint64_t mixed() const { return mixed_.load(std::memory_order_relaxed); }

private:
    struct Packet {
        std::vector<int16_t> s16;  // interleaved, already s16 at source rate
        int sample_rate = 48000;
        int channels = 2;
        std::string tag;
    };

    void WorkerMain();
    static std::vector<int16_t> ToS16(const void* data,
                                      int bytes,
                                      SimpleAudioFormat format,
                                      int channels);
    static std::vector<int16_t> ResampleStereo(const std::vector<int16_t>& in,
                                               int in_rate,
                                               int in_ch,
                                               int out_rate);

    std::shared_ptr<AudioShare> share_;
    std::mutex q_mu_;
    std::condition_variable q_cv_;
    std::queue<Packet> q_;
    size_t q_bytes_ = 0;  // guarded by q_mu_
    std::atomic<bool> stop_{false};
    std::thread worker_;
    std::atomic<uint64_t> pushed_{0};
    std::atomic<uint64_t> mixed_{0};
    std::atomic<uint64_t> dropped_{0};

    // Soft mix accumulator (worker-only).
    std::vector<float> acc_;
    size_t acc_frames_ = 0;
};
}  // namespace px
