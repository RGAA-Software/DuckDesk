#pragma once

#include <atomic>
#include <functional>

namespace px::desktop {

// Shared only as an independently owned wake signal; callbacks never retain a shell.
class FrameRefreshSignal final {
public:
    void Request(const std::function<bool()>& enqueue) {
        if (pending_.exchange(true)) return;
        if (!enqueue()) pending_.store(false);
    }
    void Consume() { pending_.store(false); }

private:
    std::atomic_bool pending_{};
};

}  // namespace px::desktop
