#pragma once

#include <memory>

namespace tc {

class AudioMixer;

// Hooks winmm waveOutOpen / waveOutWrite (and PlaySoundW as a bonus).
class HookWaveOut {
public:
    static HookWaveOut* Instance();
    bool Start(std::shared_ptr<AudioMixer> mixer);
    void Stop();

private:
    HookWaveOut() = default;
    bool installed_ = false;
};

}  // namespace tc
