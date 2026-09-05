// Local smoke test for MiniAudioCapture (WASAPI loopback).
// Plays a short sine tone on the default render device while capturing
// loopback PCM; succeeds only if non-silent audio is observed.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "miniaudio_audio_capture.h"
#include "third_party/miniaudio/miniaudio.h"

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kCaptureSeconds = 2;
constexpr int16_t kPeakThreshold = 200; // s16 absolute peak

struct CaptureStats {
    std::mutex mu;
    uint64_t bytes = 0;
    uint64_t frames = 0;
    int16_t peak = 0;
    int format_sr = 0;
    int format_ch = 0;
    int format_bits = 0;
    std::vector<char> pcm;
};

struct ToneState {
    double phase = 0.0;
};

void tone_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frame_count) {
    auto* state = static_cast<ToneState*>(device->pUserData);
    auto* out = static_cast<float*>(output);
    const double freq = 880.0;
    const double incr = 2.0 * 3.14159265358979323846 * freq / (double)device->sampleRate;
    for (ma_uint32 i = 0; i < frame_count; ++i) {
        const float s = (float)(0.25 * std::sin(state->phase));
        state->phase += incr;
        out[i * 2 + 0] = s;
        out[i * 2 + 1] = s;
    }
}

bool WriteWav(const char* path, const std::vector<char>& pcm, int sample_rate, int channels, int bits) {
    if (pcm.empty()) {
        return false;
    }
    const uint32_t data_size = (uint32_t)pcm.size();
    const uint16_t block_align = (uint16_t)(channels * bits / 8);
    const uint32_t byte_rate = (uint32_t)sample_rate * block_align;
    const uint32_t riff_size = 36 + data_size;

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&riff_size), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    uint16_t ch = (uint16_t)channels;
    uint32_t sr = (uint32_t)sample_rate;
    uint16_t bps = (uint16_t)bits;
    f.write(reinterpret_cast<const char*>(&fmt_size), 4);
    f.write(reinterpret_cast<const char*>(&audio_format), 2);
    f.write(reinterpret_cast<const char*>(&ch), 2);
    f.write(reinterpret_cast<const char*>(&sr), 4);
    f.write(reinterpret_cast<const char*>(&byte_rate), 4);
    f.write(reinterpret_cast<const char*>(&block_align), 2);
    f.write(reinterpret_cast<const char*>(&bps), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&data_size), 4);
    f.write(pcm.data(), (std::streamsize)pcm.size());
    return (bool)f;
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::printf("Testing MiniAudioCapture (OS default playback loopback)\n");

    CaptureStats stats;
    auto capture = px::MiniAudioCapture::Make();
    capture->RegisterFormatCallback([&](int sr, int ch, int bits) {
        std::lock_guard<std::mutex> lock(stats.mu);
        stats.format_sr = sr;
        stats.format_ch = ch;
        stats.format_bits = bits;
        std::printf("[format] %d Hz, %d ch, %d bit\n", sr, ch, bits);
    });
    capture->RegisterDataCallback([&](const px::DataPtr& data) {
        if (!data || data->Size() <= 0) {
            return;
        }
        std::lock_guard<std::mutex> lock(stats.mu);
        stats.bytes += (uint64_t)data->Size();
        stats.frames += (uint64_t)data->Size() / 4;
        const auto pcm_bytes = data->Bytes();
        const size_t n = pcm_bytes.size() / sizeof(int16_t);
        for (size_t i = 0; i < n; ++i) {
            int16_t sample{};
            std::memcpy(&sample, pcm_bytes.data() + i * sizeof(sample), sizeof(sample));
            const int16_t a = sample < 0 ? static_cast<int16_t>(-sample) : sample;
            if (a > stats.peak) {
                stats.peak = a;
            }
        }
        // Keep at most ~1s for WAV dump.
        const size_t max_keep = (size_t)kSampleRate * kChannels * 2;
        if (stats.pcm.size() < max_keep) {
            const size_t room = max_keep - stats.pcm.size();
            const size_t copy_n = (std::min)(room, pcm_bytes.size());
            stats.pcm.insert(stats.pcm.end(), pcm_bytes.begin(), pcm_bytes.begin() + static_cast<std::ptrdiff_t>(copy_n));
        }
    });

    if (capture->Start() != 0) {
        std::printf("FAIL: MiniAudioCapture::Start failed\n");
        return 2;
    }

    // Play a sine tone so loopback has something to capture.
    ToneState tone;
    ma_device playback{};
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = kSampleRate;
    cfg.dataCallback = tone_callback;
    cfg.pUserData = &tone;

    ma_result pr = ma_device_init(nullptr, &cfg, &playback);
    if (pr != MA_SUCCESS) {
        std::printf("FAIL: playback device init failed: %d\n", (int)pr);
        capture->Stop();
        return 3;
    }
    pr = ma_device_start(&playback);
    if (pr != MA_SUCCESS) {
        std::printf("FAIL: playback start failed: %d\n", (int)pr);
        ma_device_uninit(&playback);
        capture->Stop();
        return 4;
    }

    const auto mini_capture =
        std::dynamic_pointer_cast<px::MiniAudioCapture>(capture);
    if (!mini_capture) {
        std::printf("FAIL: capture implementation is not MiniAudioCapture\n");
        ma_device_stop(&playback);
        ma_device_uninit(&playback);
        capture->Stop();
        return 8;
    }
    mini_capture->RequestReinitForTesting("lifecycle-smoke");

    std::printf("Capturing loopback for %d seconds while playing 880Hz tone...\n", kCaptureSeconds);
    std::this_thread::sleep_for(std::chrono::seconds(kCaptureSeconds));

    ma_device_stop(&playback);
    ma_device_uninit(&playback);
    capture->Stop();

    uint64_t bytes = 0;
    uint64_t frames = 0;
    int16_t peak = 0;
    int sr = 0, ch = 0, bits = 0;
    std::vector<char> pcm;
    {
        std::lock_guard<std::mutex> lock(stats.mu);
        bytes = stats.bytes;
        frames = stats.frames;
        peak = stats.peak;
        sr = stats.format_sr;
        ch = stats.format_ch;
        bits = stats.format_bits;
        pcm = stats.pcm;
    }

    const char* wav_path = "test_miniaudio_loopback.wav";
    if (!WriteWav(wav_path, pcm, sr ? sr : kSampleRate, ch ? ch : kChannels, bits ? bits : 16)) {
        std::printf("WARN: failed to write %s\n", wav_path);
    } else {
        std::printf("Wrote %s (%zu bytes PCM)\n", wav_path, pcm.size());
    }

    std::printf("Result: format=%d/%d/%d bytes=%llu frames=%llu peak=%d threshold=%d\n",
                sr, ch, bits,
                (unsigned long long)bytes,
                (unsigned long long)frames,
                (int)peak,
                (int)kPeakThreshold);

    if (sr != kSampleRate || ch != kChannels || bits != 16) {
        std::printf("FAIL: unexpected format\n");
        return 5;
    }
    if (bytes == 0 || frames == 0) {
        std::printf("FAIL: no PCM received from loopback\n");
        return 6;
    }
    if (peak < kPeakThreshold) {
        std::printf("FAIL: captured audio looks silent (peak too low). Check mute/volume.\n");
        return 7;
    }
    if (mini_capture->SuccessfulReinitCountForTesting() == 0) {
        std::printf("FAIL: scheduled default-device reinit did not complete\n");
        return 9;
    }

    std::printf("PASS: MiniAudioCapture loopback OK\n");
    return 0;
}
