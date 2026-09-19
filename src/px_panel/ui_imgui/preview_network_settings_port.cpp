#include <mutex>
#include <utility>

#include "cloud_applications_port.h"
#include "network_settings_port.h"
#include "panel_preview.h"
#include "remote_control_port.h"
#include "security_records_port.h"
#include "server_status_port.h"
#include "settings_port.h"

namespace px::panel::ui {
namespace {

class PreviewNetworkSettingsPort final : public NetworkSettingsPort {
  public:
    NetworkSettingsState Snapshot() const override {
        const std::scoped_lock lock{mutex_};
        return state_;
    }

    void ParseConsoleAddress(std::string consoleAddress) override {
        const std::scoped_lock lock{mutex_};
        state_.settings.consoleAddress = std::move(consoleAddress);
        state_.settings.consolePort.reset();
        state_.operation = NetworkOperation::InvalidConsoleAddress;
    }

    void Verify(std::string consoleAddress) override { ParseConsoleAddress(std::move(consoleAddress)); }

    void Save(std::string consoleAddress) override {
        const std::scoped_lock lock{mutex_};
        state_.settings.consoleAddress = std::move(consoleAddress);
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

std::shared_ptr<NetworkSettingsPort> CreatePreviewNetworkSettingsPort() { return std::make_shared<PreviewNetworkSettingsPort>(); }

PanelPreview::PanelPreview()
    : PanelPreview{PanelPreviewServices{.account = CreatePreviewAccountPort(),
                                        .notifications = std::make_shared<NotificationCenter>(),
                                        .networkSettings = CreatePreviewNetworkSettingsPort(),
                                        .serverStatus = CreatePreviewServerStatusPort(),
                                        .remoteControl = CreatePreviewRemoteControlPort(),
                                        .cloudApplications = CreatePreviewCloudApplicationsPort(),
                                        .settings = CreatePreviewSettingsPort(),
                                        .securityRecords = CreatePreviewSecurityRecordsPort()}} {}

} // namespace px::panel::ui
