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
        return {.controllerDriverReady = controllerDriverReady,
                .renderReady = service.renderRunning || local.rendererConnected,
                .serviceReady = service.connected,
                .addresses = addresses_,
                .panelPort = ports.panel,
                .renderPort = ports.desktop,
                .connectedClients = local.clientConnections};
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
