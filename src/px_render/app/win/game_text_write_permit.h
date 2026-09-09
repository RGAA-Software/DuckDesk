#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace px {

// Owned by the pending request, observed by the actual websocket write predicate.
// Queueing a write never snapshots the current lease's authorization decision.
class GameTextWritePermit final {
  public:
    explicit GameTextWritePermit(std::function<bool()> authorize) : authorize_(std::move(authorize)) {}
    [[nodiscard]] bool Allows() const {
        return active_.load(std::memory_order_acquire) && authorize_ && authorize_() && active_.load(std::memory_order_acquire);
    }
    void Cancel() {
        active_.store(false, std::memory_order_release);
    }
    [[nodiscard]] static std::function<bool()> Observe(const std::shared_ptr<GameTextWritePermit>& permit) {
        return [weak = std::weak_ptr<GameTextWritePermit>(permit)] {
            const auto current = weak.lock();
            return current && current->Allows();
        };
    }

  private:
    const std::function<bool()> authorize_{};
    std::atomic_bool active_{true};
};
} // namespace px
