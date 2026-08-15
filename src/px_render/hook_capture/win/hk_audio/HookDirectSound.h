#pragma once

#include <memory>

namespace tc {

class AudioMixer;

// Hooks DirectSoundCreate / DirectSoundCreate8 → buffer Lock/Unlock.
class HookDirectSound {
public:
    static HookDirectSound* Instance();
    bool Start(std::shared_ptr<AudioMixer> mixer);
    void Stop();

private:
    HookDirectSound() = default;
    void WatcherMain();
    bool TryInstall();

    std::shared_ptr<AudioMixer> mixer_;
    void* watcher_ = nullptr;
    volatile long stop_ = 0;
    volatile long installed_ = 0;
};

}  // namespace tc
