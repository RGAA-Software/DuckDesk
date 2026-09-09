#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace px::rdp {
// Render lifetime is the reservation lifetime. Disconnect retains the owner's
// recovery rights during the existing five-second Render exit grace.
class FrontendLease final {
  public:
    [[nodiscard]] std::optional<std::uint64_t> Acquire(std::string_view logical_session) {
        std::lock_guard lock(mutex_);
        if (logical_session.empty() || logical_session.size() > 128 || active_ || (!owner_.empty() && owner_ != logical_session) ||
            generation_ == UINT64_MAX) {
            return {};
        }
        owner_ = logical_session;
        active_ = true;
        return ++generation_;
    }
    void Release(std::uint64_t generation) {
        std::lock_guard lock(mutex_);
        if (generation_ == generation) {
            active_ = false;
        }
    }

  private:
    std::mutex mutex_{};
    std::string owner_{};
    std::uint64_t generation_{0};
    bool active_{false};
};
} // namespace px::rdp
