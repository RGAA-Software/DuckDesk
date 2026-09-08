#pragma once

#include <android/native_window.h>
#include <memory>
#include <mutex>
#include <utility>

namespace px {

// JNI supplies an owning reference with ANativeWindow_release as its deleter.
// Each decoder retains a shared snapshot until output replacement or stop.
class AndroidVideoOutput final {
  public:
    explicit AndroidVideoOutput(std::shared_ptr<ANativeWindow> window) : window_(std::move(window)) {}
    [[nodiscard]] std::shared_ptr<ANativeWindow> Snapshot() const {
        std::lock_guard lock(mutex_);
        return window_;
    }
    void Replace(std::shared_ptr<ANativeWindow> window) {
        {
            std::lock_guard lock(mutex_);
            window_.swap(window);
        }
        // Releasing a retired native window happens outside the state lock.
    }

  private:
    mutable std::mutex mutex_{};
    std::shared_ptr<ANativeWindow> window_{};
};

} // namespace px
