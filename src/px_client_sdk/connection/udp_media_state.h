#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace px {

enum class UdpMediaPhase : std::uint8_t { kInactive, kProbing, kActive, kUnavailable, kStopped };
enum class UdpMediaFailure : std::uint8_t { kProbeTimeout, kInterrupted };

// Media state is independent of the reliable session. Failure never selects a
// different transport and never signals that the device/control binding is offline.
class UdpMediaState final {
  public:
    bool BeginProbe() {
        auto expected = UdpMediaPhase::kInactive;
        return phase_.compare_exchange_strong(expected, UdpMediaPhase::kProbing);
    }

    bool MarkReady() {
        auto expected = UdpMediaPhase::kProbing;
        return phase_.compare_exchange_strong(expected, UdpMediaPhase::kActive);
    }

    [[nodiscard]] std::optional<UdpMediaFailure> MarkUnavailable() {
        auto current = phase_.load();
        while (current == UdpMediaPhase::kProbing || current == UdpMediaPhase::kActive) {
            if (phase_.compare_exchange_weak(current, UdpMediaPhase::kUnavailable)) {
                return current == UdpMediaPhase::kProbing ? UdpMediaFailure::kProbeTimeout : UdpMediaFailure::kInterrupted;
            }
        }
        return std::nullopt;
    }

    void Stop() {
        phase_.store(UdpMediaPhase::kStopped);
    }
    [[nodiscard]] UdpMediaPhase Current() const {
        return phase_.load();
    }
    [[nodiscard]] bool AcceptsMedia() const {
        const auto current = Current();
        return current == UdpMediaPhase::kProbing || current == UdpMediaPhase::kActive;
    }

  private:
    std::atomic<UdpMediaPhase> phase_{UdpMediaPhase::kInactive};
};

} // namespace px
