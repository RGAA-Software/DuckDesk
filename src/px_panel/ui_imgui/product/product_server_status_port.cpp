#include "panel_product_runtime.h"

#include "environment_diagnostics.h"

#include "px_common/ip_util.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <string_view>
#include <utility>

namespace px::panel::product {
namespace {

class ProductServerStatusPort final : public ui::ServerStatusPort {
  public:
    explicit ProductServerStatusPort(std::shared_ptr<PanelProductRuntime> runtime)
        : runtime_{std::move(runtime)}, environment_{EnvironmentDiagnostics::Create(runtime_->Worker())} {
        for (const auto& address : IPUtil::ScanIPs()) {
            addresses_.push_back({.address = address.ip_addr_, .wired = address.nt_type_ == IPNetworkType::kWired});
        }
    }
    ui::ServerStatusState Snapshot() const override {
        const auto service = runtime_->Service()->Snapshot();
        const auto local = runtime_->LocalServer()->Snapshot();
        const auto ports = runtime_->Config()->Ports();
        const bool controllerDriverReady{std::filesystem::exists("C:/Windows/System32/drivers/ViGEmBus.sys")};
        ui::MachineStatus machine{};
        if (const auto information = runtime_->LocalServer()->SystemInformation()) {
            machine.available = true;
            machine.operatingSystem = information->operatingSystem;
            machine.cpuName = information->cpuName;
            machine.cpuUsagePercent = information->cpuUsagePercent;
            machine.memoryUsedBytes = information->memoryUsedBytes;
            machine.memoryTotalBytes = information->memoryTotalBytes;
            machine.disks.reserve(information->disks.size());
            for (const auto& disk : information->disks) {
                machine.disks.push_back({.mountPoint = disk.mountPoint, .availableBytes = disk.availableBytes, .totalBytes = disk.totalBytes});
            }
            machine.gpus.reserve(information->gpus.size());
            for (const auto& gpu : information->gpus) {
                machine.gpus.push_back({.name = gpu.name,
                                        .driverVersion = gpu.driverVersion,
                                        .memoryUsedBytes = gpu.memoryUsedBytes,
                                        .memoryTotalBytes = gpu.memoryTotalBytes,
                                        .utilizationPercent = gpu.utilizationPercent});
            }
        }
        return {.controllerDriverReady = controllerDriverReady,
                .renderReady = service.renderRunning || local.rendererConnected,
                .serviceReady = service.connected,
                .machine = std::move(machine),
                .addresses = addresses_,
                .panelPort = ports.panel,
                .renderPort = ports.desktop,
                .environment = environment_ ? environment_->Snapshot() : ui::EnvironmentDiagnosticsState{}};
    }
    void RestartRender() override {
        if (!runtime_->Service()->RestartRender())
            runtime_->Notify(true, "Pixels", "Render service is not connected");
    }
    void InstallControllerDriver() override {
        runtime_->Notify(false, "Pixels", "Controller driver installation is managed by the installer");
    }
    void RefreshEnvironment() override {
        if (environment_)
            static_cast<void>(environment_->Refresh());
    }
    void PerformEnvironmentAction(const ui::EnvironmentAction action) override {
        std::string_view target{};
        switch (action) {
        case ui::EnvironmentAction::OpenSoundSettings:
            target = "ms-settings:sound";
            break;
        case ui::EnvironmentAction::DownloadVisualCppRuntime:
            target = "https://aka.ms/vc14/vc_redist.x64.exe";
            break;
        case ui::EnvironmentAction::DownloadLegacyDirectXRuntime:
            target = "https://www.microsoft.com/en-us/download/details.aspx?id=35";
            break;
        case ui::EnvironmentAction::OpenAutoLoginHelp:
            target = "https://learn.microsoft.com/windows-server/user-profiles-and-logon/turn-on-automatic-logon";
            break;
        case ui::EnvironmentAction::OpenPowerSettings:
            target = "ms-settings:powersleep";
            break;
        case ui::EnvironmentAction::OpenWindowsUpdateSettings:
            target = "ms-settings:windowsupdate-restartoptions";
            break;
        case ui::EnvironmentAction::None:
            return;
        }
        static_cast<void>(SDL_OpenURL(target.data()));
    }

  private:
    std::shared_ptr<PanelProductRuntime> runtime_{};
    std::shared_ptr<EnvironmentDiagnostics> environment_{};
    std::vector<ui::NetworkAddress> addresses_{};
};

} // namespace

std::shared_ptr<ui::ServerStatusPort> CreateProductServerStatusPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    return std::make_shared<ProductServerStatusPort>(runtime);
}

} // namespace px::panel::product
