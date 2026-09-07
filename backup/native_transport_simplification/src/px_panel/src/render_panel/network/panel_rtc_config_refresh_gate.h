#ifndef PX_PANEL_RTC_CONFIG_REFRESH_GATE_H
#define PX_PANEL_RTC_CONFIG_REFRESH_GATE_H

#include <cstdint>
#include <memory>
#include <mutex>

namespace px {

struct PanelRtcConfigRefreshAttempt {
    std::uint64_t sequence = 0;
    std::uint64_t expected_revision = 0;
};

enum class PanelRtcConfigRefreshRequest {
    kStarted,
    kCoalesced,
    kStopped,
};

// Serializes Panel ICE-config pulls. Requests arriving during an HTTP pull are
// coalesced into exactly one follow-up attempt carrying the latest revision.
class PanelRtcConfigRefreshGate final {
public:
    static std::shared_ptr<PanelRtcConfigRefreshGate> Create();

    [[nodiscard]] PanelRtcConfigRefreshRequest Request(std::uint64_t expected_revision);
    [[nodiscard]] PanelRtcConfigRefreshAttempt CurrentAttempt() const;
    [[nodiscard]] bool FinishAttempt(std::uint64_t sequence);
    void AbortStart();
    void Stop();

private:
    mutable std::mutex mutex_;
    std::uint64_t request_sequence_ = 0;
    std::uint64_t expected_revision_ = 0;
    bool active_ = false;
    bool stopped_ = false;
};

} // namespace px

#endif // PX_PANEL_RTC_CONFIG_REFRESH_GATE_H
