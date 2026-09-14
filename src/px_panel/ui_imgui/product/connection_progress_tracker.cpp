#include "connection_progress_tracker.h"

#include <algorithm>
#include <array>
#include <utility>

namespace px::panel::product {
namespace {

constexpr std::array<ui::ConnectionStepKind, 6> kConnectionSteps{
    ui::ConnectionStepKind::ValidateTarget,  ui::ConnectionStepKind::ResolveDevice,  ui::ConnectionStepKind::ReachEndpoint,
    ui::ConnectionStepKind::CheckPermission, ui::ConnectionStepKind::VerifyPassword, ui::ConnectionStepKind::LaunchClient,
};

} // namespace

std::optional<std::uint64_t> ConnectionProgressTracker::Begin(const ui::ConnectionIntent intent, std::string target) {
    const std::scoped_lock lock{mutex_};
    if (progress_ && progress_->status == ui::ConnectionProgressStatus::Running)
        return std::nullopt;
    std::vector<ui::ConnectionProgressStep> steps{};
    steps.reserve(kConnectionSteps.size());
    for (const auto kind : kConnectionSteps)
        steps.push_back({.kind = kind});
    const std::uint64_t generation{nextGeneration_++};
    progress_ = {.generation = generation,
                 .intent = intent,
                 .status = ui::ConnectionProgressStatus::Running,
                 .target = std::move(target),
                 .steps = std::move(steps)};
    progress_->steps.front().state = ui::ConnectionStepState::Running;
    return generation;
}

void ConnectionProgressTracker::SetStepState(const std::uint64_t generation, const ui::ConnectionStepKind kind, const ui::ConnectionStepState state,
                                             std::string detail) {
    const std::scoped_lock lock{mutex_};
    if (!progress_ || progress_->generation != generation || progress_->status != ui::ConnectionProgressStatus::Running)
        return;
    if (state == ui::ConnectionStepState::Running) {
        for (auto& activeStep : progress_->steps) {
            if (activeStep.kind != kind && activeStep.state == ui::ConnectionStepState::Running) {
                activeStep.state = ui::ConnectionStepState::Pending;
                activeStep.detail.clear();
            }
        }
    }
    const auto step = std::ranges::find(progress_->steps, kind, &ui::ConnectionProgressStep::kind);
    if (step == progress_->steps.end())
        return;
    step->state = state;
    step->detail = std::move(detail);
}

void ConnectionProgressTracker::BeginStep(const std::uint64_t generation, const ui::ConnectionStepKind kind, std::string detail) {
    SetStepState(generation, kind, ui::ConnectionStepState::Running, std::move(detail));
}

void ConnectionProgressTracker::SucceedStep(const std::uint64_t generation, const ui::ConnectionStepKind kind, std::string detail) {
    SetStepState(generation, kind, ui::ConnectionStepState::Succeeded, std::move(detail));
}

void ConnectionProgressTracker::Fail(const std::uint64_t generation, const ui::ConnectionStepKind kind, const ui::ConnectionFailureReason reason,
                                     std::string diagnostic) {
    const std::scoped_lock lock{mutex_};
    if (!progress_ || progress_->generation != generation || progress_->status != ui::ConnectionProgressStatus::Running)
        return;
    for (auto& activeStep : progress_->steps) {
        if (activeStep.kind != kind && activeStep.state == ui::ConnectionStepState::Running) {
            activeStep.state = ui::ConnectionStepState::Pending;
            activeStep.detail.clear();
        }
    }
    if (const auto step = std::ranges::find(progress_->steps, kind, &ui::ConnectionProgressStep::kind); step != progress_->steps.end()) {
        step->state = ui::ConnectionStepState::Failed;
        step->detail = diagnostic;
    }
    progress_->status = ui::ConnectionProgressStatus::Failed;
    progress_->failure = reason;
    progress_->diagnostic = std::move(diagnostic);
}

void ConnectionProgressTracker::Complete(const std::uint64_t generation, std::string detail) {
    const std::scoped_lock lock{mutex_};
    if (!progress_ || progress_->generation != generation || progress_->status != ui::ConnectionProgressStatus::Running)
        return;
    if (const auto step = std::ranges::find(progress_->steps, ui::ConnectionStepKind::LaunchClient, &ui::ConnectionProgressStep::kind);
        step != progress_->steps.end()) {
        step->state = ui::ConnectionStepState::Succeeded;
        step->detail = std::move(detail);
    }
    progress_->status = ui::ConnectionProgressStatus::Succeeded;
}

std::optional<ui::ConnectionProgress> ConnectionProgressTracker::Snapshot() const {
    const std::scoped_lock lock{mutex_};
    return progress_;
}

} // namespace px::panel::product
