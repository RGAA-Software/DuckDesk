#pragma once

#include "cloud_applications_page.h"
#include "connection_progress_dialog.h"
#include "device_list_page.h"
#include "panel_navigation.h"
#include "notification_center.h"
#include "remote_control_page.h"
#include "server_status_page.h"
#include "settings_page.h"
#include "voice_call_consent_overlay.h"
#include "px_ui/localization.h"
#include "px_ui/px_ui_theme.h"
#include "px_desktop_shell/platform_icon_atlas.h"

#include <optional>

namespace px::panel::ui {

struct PanelPreviewAction final {
    std::optional<px::ui::Theme> selectedTheme{};
    std::optional<bool> enhancedVisualEffects{};
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

    PanelPreviewAction Draw(const px::desktop::PlatformIconAtlas& platformIcons);

  private:
    PanelPreviewAction DrawSettingsPage();

    px::ui::Localizer localizer_{};
    px::ui::Theme theme_{px::ui::Theme::Dark};
    bool initialThemePending_{true};
    bool enhancedVisualEffects_{true};
    bool initialEffectsPending_{true};
    std::shared_ptr<SettingsPort> settingsPort_{};
    std::shared_ptr<NotificationCenter> notifications_{};
    std::shared_ptr<VoiceCallConsentOverlay> voiceCallConsent_{};
    std::shared_ptr<RemoteControlPort> remoteControlPort_{};
    ConnectionProgressDialog connectionProgressDialog_;
    PanelNavigation navigation_;
    SettingsPage settings_;
    std::optional<ServerStatusPage> serverStatus_{};
    RemoteControlPage remoteControl_;
    DeviceListPage deviceList_;
    std::optional<CloudApplicationsPage> cloudApplications_{};
};

} // namespace px::panel::ui
