#pragma once

#include <Windows.h>
#include <functional>
#include <mutex>
#include <queue>

namespace px::render {

// Construct and drain on the message-loop thread. Producers may post from any
// thread; WM_NULL wakes a headless loop just as it wakes a windowed one.
class UiTaskQueue final {
  public:
    UiTaskQueue() {
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    }
    ~UiTaskQueue() {
        Close();
    }

    bool Post(std::function<void()> task) {
        if (!task) {
            return false;
        }
        std::lock_guard lock(mutex_);
        if (closed_) {
            return false;
        }
        // Post under the same lock as queue insertion. A woken consumer cannot
        // observe an empty queue between these operations. Failure retains no task.
        if (!PostThreadMessageW(thread_id_, WM_NULL, 0, 0)) {
            return false;
        }
        tasks_.push(std::move(task));
        return true;
    }

    void Drain() {
        if (GetCurrentThreadId() != thread_id_) {
            return;
        }
        std::queue<std::function<void()>> pending{};
        {
            std::lock_guard lock(mutex_);
            if (closed_) {
                return;
            }
            pending.swap(tasks_);
        }
        while (!pending.empty()) {
            {
                std::lock_guard lock(mutex_);
                if (closed_) {
                    return;
                }
            }
            pending.front()();
            pending.pop();
        }
    }

    void Close() {
        std::queue<std::function<void()>> discarded{};
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
            discarded.swap(tasks_);
        }
    }

  private:
    const DWORD thread_id_{GetCurrentThreadId()};
    std::mutex mutex_{};
    bool closed_{false};
    std::queue<std::function<void()>> tasks_{};
};

} // namespace px::render
