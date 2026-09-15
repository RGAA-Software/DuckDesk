#pragma once

#include "panel_worker.h"
#include "server_status_port.h"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace px::panel::product {

using EnvironmentProbe = std::function<std::vector<ui::EnvironmentCheckStatus>()>;

class EnvironmentDiagnostics final {
  public:
    static std::shared_ptr<EnvironmentDiagnostics> Create(const std::shared_ptr<PanelWorker>& worker, EnvironmentProbe probe = {});

    EnvironmentDiagnostics(std::shared_ptr<PanelWorker> worker, EnvironmentProbe probe);

    [[nodiscard]] ui::EnvironmentDiagnosticsState Snapshot() const;
    [[nodiscard]] bool Refresh();

  private:
    struct State final {
        mutable std::mutex mutex{};
        ui::EnvironmentDiagnosticsState snapshot{};
    };

    std::shared_ptr<PanelWorker> worker_{};
    EnvironmentProbe probe_{};
    std::shared_ptr<State> state_{};
};

[[nodiscard]] std::vector<ui::EnvironmentCheckStatus> InitialEnvironmentChecks();

} // namespace px::panel::product
