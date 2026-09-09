#pragma once
#include <chrono>
#include <optional>

namespace px::rdp {
// A lost local Service cannot leave an orphan RDP runtime holding a workspace
// forever. Restart invalidates its IPC token; only a fresh authorized launch
// may reconnect. This timer never acts on Windows user/session resources.
class RdpControlLease final {
  public:
    using Clock = std::chrono::steady_clock;
    bool Observe(bool connected, Clock::time_point now) {
        if (expired_) {
            return false;
        }
        if (connected) {
            missing_since_.reset();
            return true;
        }
        if (!missing_since_) {
            missing_since_ = now;
        }
        expired_ = now - *missing_since_ >= std::chrono::seconds(5);
        return !expired_;
    }

  private:
    std::optional<Clock::time_point> missing_since_{};
    bool expired_{false};
};
} // namespace px::rdp
