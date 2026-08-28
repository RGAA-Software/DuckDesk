#ifndef PX_RTC_ICE_RESTART_WORKFLOW_H
#define PX_RTC_ICE_RESTART_WORKFLOW_H

#include <cstdint>
#include <memory>
#include <mutex>

#include <asio2/external/asio.hpp>

#include "px_common_new/async_operation.h"

namespace px {

struct RtcIceRestartCompletion {
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
};

enum class RtcIceRestartStage {
    kIdle,
    kAwaitingConfiguration,
    kApplyingConfiguration,
    kAwaitingIce,
};

enum class RtcIceRestartDisposition {
    kStarted,
    kUpdated,
    kDuplicate,
    kStale,
};

struct RtcIceRestartBegin {
    RtcIceRestartDisposition disposition = RtcIceRestartDisposition::kDuplicate;
    std::uint64_t generation = 0;
    std::uint64_t apply_sequence = 0;
    std::shared_ptr<PxAsyncOneShot<RtcIceRestartCompletion>> operation;

    [[nodiscard]] bool StartedWorkflow() const noexcept {
        return disposition == RtcIceRestartDisposition::kStarted;
    }

    [[nodiscard]] bool ShouldApplyConfiguration() const noexcept {
        return disposition == RtcIceRestartDisposition::kStarted ||
               disposition == RtcIceRestartDisposition::kUpdated;
    }
};

struct RtcIceRestartSnapshot {
    RtcIceRestartStage stage = RtcIceRestartStage::kIdle;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    std::uint64_t apply_sequence = 0;
    std::uint64_t last_completed_revision = 0;
};

// Serializes one managed ICE-restart workflow while allowing a newer Console
// configuration revision to replace an older queued apply. All entry points
// are thread-safe because ICE, Panel, SDK and timeout completions arrive on
// different executors.
class RtcIceRestartWorkflow final {
public:
    using Operation = PxAsyncOneShot<RtcIceRestartCompletion>;

    static std::shared_ptr<RtcIceRestartWorkflow> Create(asio::any_io_executor executor);

    explicit RtcIceRestartWorkflow(asio::any_io_executor executor);

    RtcIceRestartWorkflow(const RtcIceRestartWorkflow&) = delete;
    RtcIceRestartWorkflow& operator=(const RtcIceRestartWorkflow&) = delete;

    [[nodiscard]] RtcIceRestartBegin BeginConfigurationRequest();
    [[nodiscard]] RtcIceRestartBegin ApplyConfiguration(std::uint64_t revision);

    [[nodiscard]] bool MarkApplyAccepted(std::uint64_t generation,
                                         std::uint64_t apply_sequence);
    [[nodiscard]] bool MarkApplyFailed(std::uint64_t generation,
                                       std::uint64_t apply_sequence,
                                       PxAsyncError error);
    [[nodiscard]] bool CompleteConnected();
    [[nodiscard]] bool DetachTimedOut(
        std::uint64_t generation,
        const std::shared_ptr<Operation>& operation);
    [[nodiscard]] bool Cancel(PxAsyncError error);

    [[nodiscard]] RtcIceRestartSnapshot Snapshot() const;

private:
    struct ActiveWorkflow {
        std::uint64_t generation = 0;
        std::uint64_t revision = 0;
        std::uint64_t apply_sequence = 0;
        RtcIceRestartStage stage = RtcIceRestartStage::kAwaitingConfiguration;
        std::shared_ptr<Operation> operation;
    };

    [[nodiscard]] ActiveWorkflow StartLocked(RtcIceRestartStage stage,
                                              std::uint64_t revision);

    asio::any_io_executor executor_;
    mutable std::mutex mutex_;
    std::shared_ptr<ActiveWorkflow> active_;
    std::uint64_t next_generation_ = 1;
    std::uint64_t last_completed_revision_ = 0;
};

} // namespace px

#endif // PX_RTC_ICE_RESTART_WORKFLOW_H
