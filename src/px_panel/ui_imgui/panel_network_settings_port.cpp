#include "panel_network_settings_port.h"

#include "render_panel/network/panel_network_settings_controller.h"

#include <utility>

namespace px::panel::ui {
namespace {

NetworkOperation ConvertOperation(const PanelNetworkOperation operation) {
    switch (operation) {
    case PanelNetworkOperation::Idle:
        return NetworkOperation::Idle;
    case PanelNetworkOperation::InvalidAuthorization:
        return NetworkOperation::InvalidAuthorization;
    case PanelNetworkOperation::InvalidPublicAddress:
        return NetworkOperation::InvalidPublicAddress;
    case PanelNetworkOperation::Verifying:
        return NetworkOperation::Verifying;
    case PanelNetworkOperation::Verified:
        return NetworkOperation::Verified;
    case PanelNetworkOperation::Saving:
        return NetworkOperation::Saving;
    case PanelNetworkOperation::SavedNeedsRestart:
        return NetworkOperation::SavedNeedsRestart;
    case PanelNetworkOperation::Failed:
        return NetworkOperation::Failed;
    }
    return NetworkOperation::Failed;
}

NetworkSettingsDraft ConvertSettings(const PanelNetworkSettings& settings) {
    return {
        .authorizationInfo = settings.authorizationInfo,
        .nodePublicAddress = settings.nodePublicAddress,
        .consolePort = settings.consolePort,
        .relayPort = settings.relayPort,
        .serviceManagementPort = settings.serviceManagementPort,
        .desktopConnectionPort = settings.desktopConnectionPort,
        .applicationPorts = {settings.applicationPorts.first, settings.applicationPorts.last},
        .rtcPorts = {settings.rtcPorts.first, settings.rtcPorts.last},
        .panelListeningPort = settings.panelListeningPort,
    };
}

class PanelNetworkSettingsPort final : public NetworkSettingsPort {
  public:
    explicit PanelNetworkSettingsPort(std::shared_ptr<PanelNetworkSettingsController> controller) : controller_{std::move(controller)} {}

    NetworkSettingsState Snapshot() const override {
        const auto state = controller_->Snapshot();
        return {.settings = ConvertSettings(state.settings), .operation = ConvertOperation(state.operation), .detail = state.detail};
    }

    void ParseAuthorization(std::string authorizationInfo) override {
        controller_->ParseAuthorization(std::move(authorizationInfo));
    }

    void Verify(std::string authorizationInfo) override {
        controller_->Verify(std::move(authorizationInfo));
    }

    void Save(std::string authorizationInfo, std::string nodePublicAddress) override {
        controller_->Save(std::move(authorizationInfo), std::move(nodePublicAddress));
    }

    void RestartRender() override {
        controller_->RestartRender();
    }

    void Acknowledge() override {
        controller_->Acknowledge();
    }

  private:
    std::shared_ptr<PanelNetworkSettingsController> controller_{};
};

} // namespace

std::shared_ptr<NetworkSettingsPort> CreatePanelNetworkSettingsPort(const std::shared_ptr<PxApplication>& application) {
    return std::make_shared<PanelNetworkSettingsPort>(PanelNetworkSettingsController::Create(application));
}

} // namespace px::panel::ui
