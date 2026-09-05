#include "rtc_ice_restart_workflow.h"

#include <algorithm>
#include <utility>

namespace px {

std::shared_ptr<RtcIceRestartWorkflow> RtcIceRestartWorkflow::Create(
    asio::any_io_executor executor) {
    return std::make_shared<RtcIceRestartWorkflow>(std::move(executor));
}

RtcIceRestartWorkflow::RtcIceRestartWorkflow(asio::any_io_executor executor)
    : executor_(std::move(executor)) {}

RtcIceRestartWorkflow::ActiveWorkflow RtcIceRestartWorkflow::StartLocked(
    RtcIceRestartStage stage,
    std::uint64_t revision) {
    ActiveWorkflow workflow;
    workflow.generation = next_generation_++;
    workflow.revision = revision;
    workflow.stage = stage;
    workflow.apply_sequence = stage == RtcIceRestartStage::kApplyingConfiguration ? 1 : 0;
    workflow.operation = Operation::Create(executor_);
    return workflow;
}

RtcIceRestartBegin RtcIceRestartWorkflow::BeginConfigurationRequest() {
    std::lock_guard lock(mutex_);
    if (active_) {
        return {
            .disposition = RtcIceRestartDisposition::kDuplicate,
            .generation = active_->generation,
            .apply_sequence = active_->apply_sequence,
            .operation = active_->operation,
        };
    }
    active_ = std::make_shared<ActiveWorkflow>(
        StartLocked(RtcIceRestartStage::kAwaitingConfiguration, 0));
    return {
        .disposition = RtcIceRestartDisposition::kStarted,
        .generation = active_->generation,
        .apply_sequence = 0,
        .operation = active_->operation,
    };
}

RtcIceRestartBegin RtcIceRestartWorkflow::ApplyConfiguration(std::uint64_t revision) {
    std::lock_guard lock(mutex_);
    if (revision != 0 && revision <= last_completed_revision_) {
        return {
            .disposition = RtcIceRestartDisposition::kStale,
            .generation = active_ ? active_->generation : 0,
            .apply_sequence = active_ ? active_->apply_sequence : 0,
            .operation = active_ ? active_->operation : std::shared_ptr<Operation>{},
        };
    }

    if (!active_) {
        active_ = std::make_shared<ActiveWorkflow>(
            StartLocked(RtcIceRestartStage::kApplyingConfiguration, revision));
        return {
            .disposition = RtcIceRestartDisposition::kStarted,
            .generation = active_->generation,
            .apply_sequence = active_->apply_sequence,
            .operation = active_->operation,
        };
    }

    const bool same_revision = active_->apply_sequence != 0 && active_->revision == revision;
    if (same_revision) {
        return {
            .disposition = RtcIceRestartDisposition::kDuplicate,
            .generation = active_->generation,
            .apply_sequence = active_->apply_sequence,
            .operation = active_->operation,
        };
    }
    if (revision != 0 && active_->revision != 0 && revision < active_->revision) {
        return {
            .disposition = RtcIceRestartDisposition::kStale,
            .generation = active_->generation,
            .apply_sequence = active_->apply_sequence,
            .operation = active_->operation,
        };
    }

    active_->revision = revision;
    ++active_->apply_sequence;
    active_->stage = RtcIceRestartStage::kApplyingConfiguration;
    return {
        .disposition = RtcIceRestartDisposition::kUpdated,
        .generation = active_->generation,
        .apply_sequence = active_->apply_sequence,
        .operation = active_->operation,
    };
}

bool RtcIceRestartWorkflow::MarkApplyAccepted(std::uint64_t generation,
                                               std::uint64_t apply_sequence) {
    std::lock_guard lock(mutex_);
    if (!active_ || active_->generation != generation ||
        active_->apply_sequence != apply_sequence ||
        active_->stage != RtcIceRestartStage::kApplyingConfiguration) {
        return false;
    }
    active_->stage = RtcIceRestartStage::kAwaitingIce;
    return true;
}

bool RtcIceRestartWorkflow::MarkApplyFailed(std::uint64_t generation,
                                             std::uint64_t apply_sequence,
                                             PxAsyncError error) {
    std::shared_ptr<Operation> operation;
    {
        std::lock_guard lock(mutex_);
        if (!active_ || active_->generation != generation ||
            active_->apply_sequence != apply_sequence ||
            active_->stage != RtcIceRestartStage::kApplyingConfiguration) {
            return false;
        }
        operation = active_->operation;
        active_.reset();
    }
    return operation->TryFail(std::move(error));
}

bool RtcIceRestartWorkflow::CompleteConnected() {
    std::shared_ptr<Operation> operation;
    RtcIceRestartCompletion completion;
    {
        std::lock_guard lock(mutex_);
        if (!active_ || active_->stage == RtcIceRestartStage::kAwaitingConfiguration) {
            return false;
        }
        completion.generation = active_->generation;
        completion.revision = active_->revision;
        if (completion.revision != 0) {
            last_completed_revision_ = std::max(last_completed_revision_, completion.revision);
        }
        operation = active_->operation;
        active_.reset();
    }
    return operation->TryComplete(PxResult<RtcIceRestartCompletion>::Success(completion));
}

bool RtcIceRestartWorkflow::DetachTimedOut(
    std::uint64_t generation,
    const std::shared_ptr<Operation>& operation) {
    std::lock_guard lock(mutex_);
    if (!active_ || active_->generation != generation || active_->operation != operation) {
        return false;
    }
    active_.reset();
    return true;
}

bool RtcIceRestartWorkflow::Cancel(PxAsyncError error) {
    std::shared_ptr<Operation> operation;
    {
        std::lock_guard lock(mutex_);
        if (!active_) {
            return false;
        }
        operation = active_->operation;
        active_.reset();
    }
    return operation->TryFail(std::move(error));
}

RtcIceRestartSnapshot RtcIceRestartWorkflow::Snapshot() const {
    std::lock_guard lock(mutex_);
    if (!active_) {
        return {
            .stage = RtcIceRestartStage::kIdle,
            .last_completed_revision = last_completed_revision_,
        };
    }
    return {
        .stage = active_->stage,
        .generation = active_->generation,
        .revision = active_->revision,
        .apply_sequence = active_->apply_sequence,
        .last_completed_revision = last_completed_revision_,
    };
}

} // namespace px
