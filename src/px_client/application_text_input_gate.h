#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace px {

// Shared by all video views of one Client instance. Capture the generation before
// queuing an event: an old queued event must never acquire a newer authorization.
class ApplicationTextInputGate final {
  public:
    void Set(bool suspended, std::string generation) {
        const std::lock_guard lock(mutex_);
        suspended_ = suspended;
        generation_ = std::move(generation);
    }

    [[nodiscard]] std::optional<std::string> OrdinaryInputGeneration() const {
        const std::lock_guard lock(mutex_);
        if (suspended_)
            return std::nullopt;
        return generation_;
    }

  private:
    mutable std::mutex mutex_{};
    bool suspended_{};
    std::string generation_{};
};

} // namespace px
