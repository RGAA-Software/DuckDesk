#pragma once

#include <memory>

#include "AudioMixer.h"
#include "SimpleAudioFormatConverter.h"

namespace tc {

// Shared mixer used by all audio API hooks in this process.
std::shared_ptr<AudioMixer>& GlobalAudioMixer();
void SetGlobalAudioMixer(std::shared_ptr<AudioMixer> mixer);

// WASAPI ReleaseBuffer is the final mix on Windows. When it is actively
// posting, skip XAudio2/DirectSound/waveOut to avoid double-counting the
// same stream (BGM+SFX already mixed into the render client).
void NoteWasapiCapture();
bool WasapiCaptureActive(uint32_t within_ms = 200);

inline void PushHookedPcm(const void* data,
                          int bytes,
                          SimpleAudioFormat format,
                          int sample_rate,
                          int channels,
                          const char* tag) {
    auto mix = GlobalAudioMixer();
    if (mix && data && bytes > 0) {
        mix->Push(data, bytes, format, sample_rate, channels, tag);
    }
}

bool BufferHasEnergy(const void* data, int bytes, SimpleAudioFormat fmt);
// Peak amplitude in ~0..1+ range (s16 normalized to 1.0 == 32768).
float BufferPeak(const void* data, int bytes, SimpleAudioFormat fmt);

}  // namespace tc
