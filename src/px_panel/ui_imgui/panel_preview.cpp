#include "panel_preview.h"
#include "panel_layout.h"

#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <utility>

namespace px::panel::ui {

PanelPreview::PanelPreview(PanelPreviewServices services)
    : settingsPort_{services.settings}, notifications_{std::move(services.notifications)}, voiceCallConsent_{std::move(services.voiceCallConsent)},
      remoteControlPort_{services.remoteControl}, connectionProgressDialog_{remoteControlPort_}, navigation_{std::move(services.account)},
      settings_{std::move(services.networkSettings), services.settings}, serverStatus_{std::move(services.serverStatus)},
      remoteControl_{services.remoteControl}, deviceList_{std::move(services.remoteControl)},
      cloudApplications_{std::move(services.cloudApplications)}, securityRecords_{std::move(services.securityRecords)} {
    const auto appearance = settingsPort_->Snapshot();
    localizer_.SetLanguage(appearance.language);
    theme_ = appearance.theme;
    enhancedVisualEffects_ = appearance.enhancedVisualEffects;
}

PanelPreviewAction PanelPreview::DrawSettingsPage() {
    PanelPreviewAction action{};
    ImGui::BeginChild("SettingsPage", ImVec2{-px::ui::Scale(10.0F), -layout::PageBottomInset()}, ImGuiChildFlags_None);
    px::ui::PageTitle(localizer_.Text(px::ui::TextId::Settings));
    action.selectedTheme = settings_.Draw(localizer_, theme_);
    const bool currentEffects{settingsPort_->Snapshot().enhancedVisualEffects};
    if (currentEffects != enhancedVisualEffects_) {
        enhancedVisualEffects_ = currentEffects;
        action.enhancedVisualEffects = currentEffects;
    }
    ImGui::EndChild();
    return action;
}

PanelPreviewAction PanelPreview::Draw(const px::desktop::PlatformIconAtlas& platformIcons) {
    const NavigationAction navigationAction{navigation_.Draw(localizer_)};
    ImGui::SameLine(0.0F, layout::NavigationGap());
    if (navigationAction.selectedPage == PanelPage::Settings) {
        auto action = DrawSettingsPage();
        if (initialThemePending_) {
            action.selectedTheme = theme_;
            initialThemePending_ = false;
        }
        if (initialEffectsPending_) {
            action.enhancedVisualEffects = enhancedVisualEffects_;
            initialEffectsPending_ = false;
        }
        action.exitRequested = navigationAction.exitRequested;
        notifications_->Draw();
        if (voiceCallConsent_)
            voiceCallConsent_->Draw(localizer_);
        connectionProgressDialog_.Draw(localizer_);
        return action;
    }
    const ImGuiWindowFlags pageFlags{navigationAction.selectedPage == PanelPage::RemoteControl
                                         ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                                         : ImGuiWindowFlags_None};
    ImGui::BeginChild("PageContent", ImVec2{-px::ui::Scale(10.0F), -layout::PageBottomInset()}, ImGuiChildFlags_None, pageFlags);
    if (navigationAction.selectedPage == PanelPage::RemoteControl) {
        remoteControl_.Draw(localizer_, platformIcons);
    } else if (navigationAction.selectedPage == PanelPage::DeviceList) {
        deviceList_.Draw(localizer_, platformIcons);
    } else if (navigationAction.selectedPage == PanelPage::CloudApplications) {
        cloudApplications_.Draw(localizer_);
    } else if (navigationAction.selectedPage == PanelPage::ServerStatus) {
        serverStatus_.Draw(localizer_);
    } else if (navigationAction.selectedPage == PanelPage::Security) {
        securityRecords_.Draw(localizer_);
    }
    ImGui::EndChild();
    notifications_->Draw();
    if (voiceCallConsent_)
        voiceCallConsent_->Draw(localizer_);
    connectionProgressDialog_.Draw(localizer_);
    PanelPreviewAction action{.exitRequested = navigationAction.exitRequested};
    if (initialThemePending_) {
        action.selectedTheme = theme_;
        initialThemePending_ = false;
    }
    if (initialEffectsPending_) {
        action.enhancedVisualEffects = enhancedVisualEffects_;
        initialEffectsPending_ = false;
    }
    return action;
}

} // namespace px::panel::ui
