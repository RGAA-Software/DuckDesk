#include "environment_diagnostics.h"

#include "windows_environment_probe.h"

#include <cstdint>
#include <utility>

namespace px::panel::product {

namespace {

std::vector<ui::EnvironmentCheckStatus> UnavailableEnvironmentChecks() {
    auto checks = InitialEnvironmentChecks();
    for (auto& check : checks)
        check.state = ui::EnvironmentCheckState::Unavailable;
    return checks;
}

} // namespace

std::vector<ui::EnvironmentCheckStatus> InitialEnvironmentChecks() {
    using enum ui::EnvironmentCheckId;
    return {
        {.id = Audio},          {.id = VisualCppRuntime}, {.id = LegacyDirectXRuntime}, {.id = WindowsAutoLogin},
        {.id = DisplayTimeout}, {.id = SleepTimeout},     {.id = HighPerformanceMode},  {.id = PendingRestart},
    };
}

std::shared_ptr<EnvironmentDiagnostics> EnvironmentDiagnostics::Create(const std::shared_ptr<PanelWorker>& worker, EnvironmentProbe probe) {
    if (!worker)
        return {};
    if (!probe)
        probe = ProbeWindowsEnvironment;
    const auto diagnostics = std::make_shared<EnvironmentDiagnostics>(worker, std::move(probe));
    static_cast<void>(diagnostics->Refresh());
    return diagnostics;
}

EnvironmentDiagnostics::EnvironmentDiagnostics(std::shared_ptr<PanelWorker> worker, EnvironmentProbe probe)
    : worker_{std::move(worker)}, probe_{std::move(probe)}, state_{std::make_shared<State>()} {
    state_->snapshot.checks = InitialEnvironmentChecks();
}

ui::EnvironmentDiagnosticsState EnvironmentDiagnostics::Snapshot() const {
    const std::scoped_lock lock{state_->mutex};
    return state_->snapshot;
}

bool EnvironmentDiagnostics::Refresh() {
    std::uint64_t generation{};
    {
        const std::scoped_lock lock{state_->mutex};
        if (state_->snapshot.refreshing)
            return false;
        state_->snapshot.refreshing = true;
        generation = ++state_->snapshot.generation;
        state_->snapshot.checks = InitialEnvironmentChecks();
    }

    const auto state = state_;
    const auto probe = probe_;
    if (worker_->Post([state, probe, generation] {
            auto checks = UnavailableEnvironmentChecks();
            try {
                checks = probe();
            } catch (...) {
            }
            const std::scoped_lock lock{state->mutex};
            if (state->snapshot.generation != generation)
                return;
            state->snapshot.checks = std::move(checks);
            state->snapshot.refreshing = false;
        })) {
        return true;
    }

    const std::scoped_lock lock{state_->mutex};
    if (state_->snapshot.generation == generation) {
        state_->snapshot.checks = UnavailableEnvironmentChecks();
        state_->snapshot.refreshing = false;
    }
    return false;
}

} // namespace px::panel::product
