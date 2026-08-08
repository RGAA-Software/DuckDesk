#pragma once

#include <Windows.h>
#include <mmreg.h>

#include <cstring>
#include <memory>

#include "AudioMixer.h"
#include "SimpleAudioFormatConverter.h"

namespace tc {

// Shared mixer used by all audio API hooks in this process.
std::shared_ptr<AudioMixer> GlobalAudioMixer();
void SetGlobalAudioMixer(std::shared_ptr<AudioMixer> mixer);

// WASAPI ReleaseBuffer is the final mix on Windows. When it is actively
// posting, skip XAudio2/DirectSound/waveOut to avoid double-counting the
// same stream (BGM+SFX already mixed into the render client).
void NoteWasapiCapture();
bool WasapiCaptureActive(uint32_t within_ms = 1000);

// Fallback-source frame dropped because the WASAPI path is live (counted,
// throttled log inside).
void NoteFallbackSuppressed(const char* tag);

// Resolve WAVEFORMATEX (incl. WAVEFORMATEXTENSIBLE SubFormat) to a format the
// mixer actually supports. Returns false for unknown / unsupported layouts
// (8/24/32-bit int PCM, compressed, ...) — caller must drop the frame.
bool ResolveWaveFormat(const WAVEFORMATEX* fmt, SimpleAudioFormat& out);

// DetourUpdateThread wrapper for attach/detach transactions. Kept as the
// current thread only on purpose — see the implementation comment (suspending
// every game thread during commit hung UE4 game startup in practice).
void DetourUpdateAllThreads();

inline void PushHookedPcm(const void* data,
                          int bytes,
                          SimpleAudioFormat format,
                          int sample_rate,
                          int channels,
                          const char* tag) {
    auto mix = GlobalAudioMixer();
    if (!mix || !data || bytes <= 0) {
        return;
    }
    // WASAPI render client already carries the final mix; while it is live,
    // drop fallback sources outright instead of "mixing" (concatenating) them.
    const bool is_wasapi = tag && std::strcmp(tag, "WASAPI") == 0;
    if (!is_wasapi && WasapiCaptureActive()) {
        NoteFallbackSuppressed(tag);
        return;
    }
    mix->Push(data, bytes, format, sample_rate, channels, tag);
}

bool BufferHasEnergy(const void* data, int bytes, SimpleAudioFormat fmt);
// Peak amplitude in ~0..1+ range (s16 normalized to 1.0 == 32768).
float BufferPeak(const void* data, int bytes, SimpleAudioFormat fmt);

}  // namespace tc
