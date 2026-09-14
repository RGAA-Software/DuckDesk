#include "panel_product_runtime.h"

#include "px_common/ip_util.h"

#include <filesystem>
#include <utility>

namespace px::panel::product {
namespace {

class ProductServerStatusPort final : public ui::ServerStatusPort {
  public:
    explicit ProductServerStatusPort(std::shared_ptr<PanelProductRuntime> runtime) : runtime_{std::move(runtime)} {
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
                .renderPort = ports.desktop};
    }
    void RestartRender() override {
        if (!runtime_->Service()->RestartRender())
            runtime_->Notify(true, "Pixels", "Render service is not connected");
    }
    void InstallControllerDriver() override {
        runtime_->Notify(false, "Pixels", "Controller driver installation is managed by the installer");
    }

  private:
    std::shared_ptr<PanelProductRuntime> runtime_{};
    std::vector<ui::NetworkAddress> addresses_{};
};

} // namespace

std::shared_ptr<ui::ServerStatusPort> CreateProductServerStatusPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    return std::make_shared<ProductServerStatusPort>(runtime);
}

} // namespace px::panel::product
