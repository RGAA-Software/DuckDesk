#pragma once

#include <filesystem>
#include <memory>
#include <thread>

namespace px::panel::product {

class PanelOsInfoSupervisor final {
  public:
    static std::shared_ptr<PanelOsInfoSupervisor> Create(const std::filesystem::path& executableDirectory, int panelPort);

    explicit PanelOsInfoSupervisor(std::shared_ptr<const struct OsInfoLaunchState> state);
    ~PanelOsInfoSupervisor();

    PanelOsInfoSupervisor(const PanelOsInfoSupervisor&) = delete;
    PanelOsInfoSupervisor& operator=(const PanelOsInfoSupervisor&) = delete;

    void Stop();

  private:
    std::shared_ptr<const OsInfoLaunchState> state_{};
    std::jthread worker_{};
};

} // namespace px::panel::product
