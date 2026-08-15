#pragma once

#include <memory>

namespace px {

class AudioMixer;

// XAudio2Create → CreateSourceVoice → SubmitSourceBuffer (all engines / voices).
class HookXAudio2 {
public:
    static HookXAudio2* Instance();
    bool Start(std::shared_ptr<AudioMixer> mixer);
    void Stop();

private:
    HookXAudio2() = default;
    void WatcherMain();
    bool TryInstall();

    std::shared_ptr<AudioMixer> mixer_;
    void* watcher_ = nullptr;
    volatile long stop_ = 0;
    volatile long installed_ = 0;
};

}  // namespace px
