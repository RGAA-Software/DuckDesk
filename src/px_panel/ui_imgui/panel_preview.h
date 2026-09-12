#pragma once

#include "cloud_applications_page.h"
#include "panel_navigation.h"
#include "notification_center.h"
#include "remote_control_page.h"
#include "server_status_page.h"
#include "security_records_page.h"
#include "settings_page.h"
#include "voice_call_consent_overlay.h"
#include "px_ui/localization.h"
#include "px_ui/px_ui_theme.h"

#include <optional>

namespace px::panel::ui {

struct PanelPreviewAction final {
    std::optional<px::ui::Theme> selectedTheme{};
    bool exitRequested{false};
};

struct PanelPreviewServices final {
    std::shared_ptr<AccountPort> account{};
    std::shared_ptr<NotificationCenter> notifications{};
    std::shared_ptr<NetworkSettingsPort> networkSettings{};
    std::shared_ptr<ServerStatusPort> serverStatus{};
    std::shared_ptr<RemoteControlPort> remoteControl{};
    std::shared_ptr<CloudApplicationsPort> cloudApplications{};
    std::shared_ptr<SettingsPort> settings{};
    std::shared_ptr<SecurityRecordsPort> securityRecords{};
    std::shared_ptr<VoiceCallConsentOverlay> voiceCallConsent{};
};

class PanelPreview final {
  public:
    PanelPreview();
    explicit PanelPreview(PanelPreviewServices services);

    PanelPreviewAction Draw();

  private:
    PanelPreviewAction DrawSettingsPage();

    px::ui::Localizer localizer_{};
    px::ui::Theme theme_{px::ui::Theme::Dark};
    bool initialThemePending_{true};
    std::shared_ptr<SettingsPort> settingsPort_{};
    std::shared_ptr<NotificationCenter> notifications_{};
    std::shared_ptr<VoiceCallConsentOverlay> voiceCallConsent_{};
    PanelNavigation navigation_;
    SettingsPage settings_;
    ServerStatusPage serverStatus_;
    RemoteControlPage remoteControl_;
    CloudApplicationsPage cloudApplications_;
    SecurityRecordsPage securityRecords_;
};

} // namespace px::panel::ui
