#include "network_settings_port.h"

#include "panel_preview.h"
#include "server_status_port.h"

#include <mutex>
#include <utility>

namespace px::panel::ui {
namespace {

class PreviewNetworkSettingsPort final : public NetworkSettingsPort {
  public:
    NetworkSettingsState Snapshot() const override {
        const std::scoped_lock lock{mutex_};
        return state_;
    }

    void ParseAuthorization(std::string authorizationInfo) override {
        const std::scoped_lock lock{mutex_};
        state_.settings.authorizationInfo = std::move(authorizationInfo);
        state_.settings.consolePort.reset();
        state_.settings.relayPort.reset();
        state_.operation = NetworkOperation::InvalidAuthorization;
    }

    void Verify(std::string authorizationInfo) override {
        ParseAuthorization(std::move(authorizationInfo));
    }

    void Save(std::string authorizationInfo, std::string nodePublicAddress) override {
        const std::scoped_lock lock{mutex_};
        state_.settings.authorizationInfo = std::move(authorizationInfo);
        state_.settings.nodePublicAddress = std::move(nodePublicAddress);
        state_.operation = NetworkOperation::Idle;
    }

    void RestartRender() override {}

    void Acknowledge() override {
        const std::scoped_lock lock{mutex_};
        state_.operation = NetworkOperation::Idle;
    }

  private:
    mutable std::mutex mutex_{};
    NetworkSettingsState state_{};
};

} // namespace

std::shared_ptr<NetworkSettingsPort> CreatePreviewNetworkSettingsPort() {
    return std::make_shared<PreviewNetworkSettingsPort>();
}

PanelPreview::PanelPreview()
    : PanelPreview{PanelPreviewServices{.networkSettings = CreatePreviewNetworkSettingsPort(), .serverStatus = CreatePreviewServerStatusPort()}} {}

} // namespace px::panel::ui
