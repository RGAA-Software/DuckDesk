#pragma once

#include "audio_capture.h"

#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>

namespace px {

// Host-side WASAPI process-loopback (Application Audio Capture).
// Same API as test_game_audio_loopback.cpp (ActivateAudioInterfaceAsync).
// Production keeps this path; MiniAudio PID loopback is patched/debuggable via
// test_miniaudio_pid_loopback but not wired here yet.
class ProcessLoopbackAudioCapture : public IAudioCapture {
public:
    static AudioCapturePtr Make(uint32_t process_id);

    explicit ProcessLoopbackAudioCapture(uint32_t process_id);
    ~ProcessLoopbackAudioCapture();

    int Start() override;
    int Pause() override;
    int Stop() override;
    bool IsFatalStop() const override;

private:
    struct State;
    static void CaptureThreadMain(
        const std::shared_ptr<State>& state, std::stop_token stop_token);
    static void NotifyStopOnce(const std::shared_ptr<State>& state);

    std::shared_ptr<State> state_;
};

}  // namespace px
