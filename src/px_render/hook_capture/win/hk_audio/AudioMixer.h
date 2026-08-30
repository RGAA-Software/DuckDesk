#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "SimpleAudioFormatConverter.h"

namespace px {

class AudioShare;
class Data;
class Thread;

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
    void Push(std::span<const std::byte> data,
              SimpleAudioFormat format,
              int sample_rate,
              int channels,
              std::string_view source_tag);

    // Idempotent. Safe when called by the worker-side sender callback.
    void Stop();

    uint64_t pushed() const;
    uint64_t mixed() const;

private:
    struct Packet {
        std::vector<int16_t> s16;  // interleaved, already s16 at source rate
        int sample_rate = 48000;
        int channels = 2;
        std::string tag;
    };

    class State;

    static void WorkerMain(const std::shared_ptr<State>& state);
    static std::vector<int16_t> ToS16(std::span<const std::byte> data,
                                      SimpleAudioFormat format,
                                      int channels);
    static std::vector<int16_t> ResampleStereo(const std::vector<int16_t>& in,
                                               int in_rate,
                                               int in_ch,
                                               int out_rate);

    std::shared_ptr<State> state_;
    std::shared_ptr<Thread> worker_;
};
}  // namespace px
