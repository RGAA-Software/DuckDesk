#include "AudioHookCommon.h"

#include <Windows.h>

#include <atomic>

namespace tc {
namespace {
std::shared_ptr<AudioMixer> g_mixer;
std::atomic<ULONGLONG> g_wasapi_tick{0};
}

std::shared_ptr<AudioMixer>& GlobalAudioMixer() {
    return g_mixer;
}

void SetGlobalAudioMixer(std::shared_ptr<AudioMixer> mixer) {
    g_mixer = std::move(mixer);
}

void NoteWasapiCapture() {
    g_wasapi_tick.store(GetTickCount64(), std::memory_order_relaxed);
}

bool WasapiCaptureActive(uint32_t within_ms) {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG last = g_wasapi_tick.load(std::memory_order_relaxed);
    if (last == 0) {
        return false;
    }
    return (now - last) <= within_ms;
}

float BufferPeak(const void* data, int bytes, SimpleAudioFormat fmt) {
    if (!data || bytes <= 0) {
        return 0.f;
    }
    float peak = 0.f;
    if (fmt == SimpleAudioFormat::kPCM_F32) {
        const int n = bytes / static_cast<int>(sizeof(float));
        const auto* f = static_cast<const float*>(data);
        for (int i = 0; i < n; i++) {
            float a = f[i];
            if (a < 0) {
                a = -a;
            }
            if (a > peak) {
                peak = a;
            }
        }
        return peak;
    }
    const int n = bytes / static_cast<int>(sizeof(int16_t));
    const auto* s = static_cast<const int16_t*>(data);
    for (int i = 0; i < n; i++) {
        int v = s[i];
        if (v < 0) {
            v = -v;
        }
        const float a = static_cast<float>(v) / 32768.f;
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

bool BufferHasEnergy(const void* data, int bytes, SimpleAudioFormat fmt) {
    // Accept any non-trivial sample. Do not reject "hot" floats (>1.0) — some
    // engines temporarily exceed full-scale; clamping happens in the mixer.
    return BufferPeak(data, bytes, fmt) > 1.0e-5f;
}

}  // namespace tc
