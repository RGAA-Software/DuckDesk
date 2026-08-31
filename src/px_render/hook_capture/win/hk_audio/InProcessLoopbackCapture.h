#pragma once

#include <atomic>
#include <memory>
#include <thread>

namespace px {

class AudioShare;

// Captures this process's audio via WASAPI process-loopback (Win10 2004+).
// Used by the in-process hook path when OS supports PID loopback; falls back
// to GetBuffer/ReleaseBuffer hooks when this is unavailable.
class InProcessLoopbackCapture {
public:
    InProcessLoopbackCapture() = default;
    ~InProcessLoopbackCapture();

    InProcessLoopbackCapture(const InProcessLoopbackCapture&) = delete;
    InProcessLoopbackCapture& operator=(const InProcessLoopbackCapture&) = delete;

    bool Start(std::shared_ptr<AudioShare> share, uint32_t pid);
    void Stop();
    bool running() const;

private:
    struct State;
    static void ThreadMain(const std::shared_ptr<State>& state, uint32_t pid);

    std::shared_ptr<State> state_;
    std::thread thread_;
};

}  // namespace px
