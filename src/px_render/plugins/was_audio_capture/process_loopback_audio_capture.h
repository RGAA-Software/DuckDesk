#pragma once

#include "audio_capture.h"

#include <atomic>
#include <cstdint>
#include <mutex>
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
    bool IsFatalStop() const override { return fatal_stop_.load(); }

private:
    void CaptureThreadMain();
    void NotifyStopOnce();

    uint32_t pid_ = 0;
    std::mutex mu_;
    std::atomic<bool> want_running_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_notified_{false};
    std::atomic<bool> fatal_stop_{false};
    std::thread worker_;
    int samples_ = 48000;
    int channels_ = 2;
    int bits_ = 16;
};

}  // namespace px
