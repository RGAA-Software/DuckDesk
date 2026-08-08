#include "AudioHookCommon.h"

#include <ks.h>
#include <ksmedia.h>
#include <detours/detours.h>

#include <atomic>

#include "tc_common_new/log.h"

namespace tc {
namespace {
std::atomic<std::shared_ptr<AudioMixer>> g_mixer;
std::atomic<ULONGLONG> g_wasapi_tick{0};
}

std::shared_ptr<AudioMixer> GlobalAudioMixer() {
    return g_mixer.load();
}

void SetGlobalAudioMixer(std::shared_ptr<AudioMixer> mixer) {
    g_mixer.store(std::move(mixer));
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

void NoteFallbackSuppressed(const char* tag) {
    static std::atomic<uint64_t> s_n{0};
    const auto n = s_n.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || (n % 500) == 0) {
        LOGI("AudioHook: fallback source dropped (WASAPI active) tag={} n={}",
             tag ? tag : "?", n);
    }
}

bool ResolveWaveFormat(const WAVEFORMATEX* fmt, SimpleAudioFormat& out) {
    if (!fmt || fmt->nChannels == 0 || fmt->nSamplesPerSec == 0) {
        return false;
    }
    WORD tag = fmt->wFormatTag;
    const WORD bits = fmt->wBitsPerSample;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (fmt->cbSize < 22) {
            return false;
        }
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            tag = WAVE_FORMAT_IEEE_FLOAT;
        } else if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            tag = WAVE_FORMAT_PCM;
        } else {
            return false;
        }
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && bits == 32) {
        out = SimpleAudioFormat::kPCM_F32;
        return true;
    }
    if (tag == WAVE_FORMAT_PCM && bits == 16) {
        out = SimpleAudioFormat::kPCM_S16;
        return true;
    }
    // 8/24/32-bit int PCM and anything else: unsupported, do not guess.
    return false;
}

void DetourUpdateAllThreads() {
    // Originally this enumerated every thread in the process (Toolhelp32) and
    // DetourUpdateThread'ed each so no thread could run through a half-written
    // Detours patch. In practice that made DetourTransactionCommit suspend ALL
    // game threads right in the middle of game startup and hung UE4 titles
    // intermittently (bisected 2026-08-08: all-thread suspend ⇒ game freezes
    // at ~80 MB before creating its window; current-thread only ⇒ stable).
    // The half-patch window this leaves is microscopic and these exports
    // (waveOut*/DirectSoundCreate8/XAudio2Create) are called rarely, so the
    // current-thread update is the acceptable trade-off.
    DetourUpdateThread(GetCurrentThread());
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
