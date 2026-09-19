#pragma once

#include "account_port.h"
#include "cloud_applications_port.h"
#include "network_settings_port.h"
#include "notification_center.h"
#include "remote_control_port.h"
#include "security_records_port.h"
#include "server_status_port.h"
#include "settings_port.h"
#include "voice_call_consent_overlay.h"

#include "panel_client_launcher.h"
#include "panel_audit_store.h"
#include "panel_config_store.h"
#include "panel_console_session.h"
#include "panel_local_server.h"
#include "panel_node_presence.h"
#include "panel_os_info_supervisor.h"
#include "panel_service_bridge.h"
#include "panel_worker.h"

#include <filesystem>
#include <functional>
#include <memory>

namespace px::panel::product {

class PanelProductRuntime final {
  public:
    static std::shared_ptr<PanelProductRuntime> Create(const std::filesystem::path& executableDirectory,
                                                       const std::shared_ptr<ui::NotificationCenter>& notifications);
    PanelProductRuntime(std::shared_ptr<PanelConfigStore> config, std::shared_ptr<PanelConsoleSession> console,
                        std::shared_ptr<PanelClientLauncher> launcher, std::shared_ptr<PanelServiceBridge> service,
                        std::shared_ptr<PanelLocalServer> localServer, std::shared_ptr<PanelNodePresence> nodePresence,
                        std::shared_ptr<PanelOsInfoSupervisor> osInfoSupervisor, std::shared_ptr<PanelAuditStore> auditStore,
                        std::shared_ptr<PanelWorker> worker, std::shared_ptr<ui::NotificationCenter> notifications);
    ~PanelProductRuntime();

    [[nodiscard]] const std::shared_ptr<PanelConfigStore>& Config() const;
    [[nodiscard]] const std::shared_ptr<PanelConsoleSession>& Console() const;
    [[nodiscard]] const std::shared_ptr<PanelClientLauncher>& Launcher() const;
    [[nodiscard]] const std::shared_ptr<PanelServiceBridge>& Service() const;
    [[nodiscard]] const std::shared_ptr<PanelLocalServer>& LocalServer() const;
    [[nodiscard]] const std::shared_ptr<PanelAuditStore>& AuditStore() const;
    [[nodiscard]] const std::shared_ptr<PanelWorker>& Worker() const;
    [[nodiscard]] const std::shared_ptr<ui::NotificationCenter>& Notifications() const;
    void Notify(bool error, std::string title, std::string message) const;

  private:
    std::shared_ptr<PanelConfigStore> config_{};
    std::shared_ptr<PanelConsoleSession> console_{};
    std::shared_ptr<PanelClientLauncher> launcher_{};
    std::shared_ptr<PanelServiceBridge> service_{};
    std::shared_ptr<PanelLocalServer> localServer_{};
    std::shared_ptr<PanelNodePresence> nodePresence_{};
    std::shared_ptr<PanelOsInfoSupervisor> osInfoSupervisor_{};
    std::shared_ptr<PanelAuditStore> auditStore_{};
    std::shared_ptr<PanelWorker> worker_{};
    std::shared_ptr<ui::NotificationCenter> notifications_{};
};

std::shared_ptr<ui::AccountPort> CreateProductAccountPort(const std::shared_ptr<PanelProductRuntime>& runtime);
std::shared_ptr<ui::NetworkSettingsPort> CreateProductNetworkSettingsPort(const std::shared_ptr<PanelProductRuntime>& runtime);
std::shared_ptr<ui::ServerStatusPort> CreateProductServerStatusPort(const std::shared_ptr<PanelProductRuntime>& runtime);
std::shared_ptr<ui::RemoteControlPort> CreateProductRemoteControlPort(const std::shared_ptr<PanelProductRuntime>& runtime);
std::shared_ptr<ui::CloudApplicationsPort> CreateProductCloudApplicationsPort(const std::shared_ptr<PanelProductRuntime>& runtime);
std::shared_ptr<ui::SettingsPort> CreateProductSettingsPort(const std::shared_ptr<PanelProductRuntime>& runtime);
std::shared_ptr<ui::SecurityRecordsPort> CreateProductSecurityRecordsPort(const std::shared_ptr<PanelProductRuntime>& runtime);
std::shared_ptr<ui::VoiceCallConsentOverlay> CreateProductVoiceCallConsentOverlay(const std::shared_ptr<PanelProductRuntime>& runtime,
                                                                                  std::function<void()> showPanel);

} // namespace px::panel::product
