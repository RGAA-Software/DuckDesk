#pragma once

#include <atomic>
#include <memory>
#include <thread>

namespace tc {

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
    bool running() const { return running_.load(std::memory_order_acquire); }

private:
    void ThreadMain(uint32_t pid);

    std::shared_ptr<AudioShare> share_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
};

}  // namespace tc
