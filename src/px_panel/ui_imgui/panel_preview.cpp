#include "panel_preview.h"

#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <algorithm>
#include <string_view>
#include <utility>

namespace px::panel::ui {
namespace {

void DrawDisabledText(const std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
}

} // namespace

PanelPreview::PanelPreview(PanelPreviewServices services)
    : settingsPort_{services.settings}, notifications_{std::move(services.notifications)},
      voiceCallConsent_{std::move(services.voiceCallConsent)}, navigation_{std::move(services.account)},
      settings_{std::move(services.networkSettings), services.settings},
      serverStatus_{std::move(services.serverStatus)},
      remoteControl_{std::move(services.remoteControl)}, cloudApplications_{std::move(services.cloudApplications)},
      securityRecords_{std::move(services.securityRecords)} {
    const auto appearance = settingsPort_->Snapshot();
    localizer_.SetLanguage(appearance.language);
    theme_ = appearance.theme;
}

PanelPreviewAction PanelPreview::DrawSettingsPage() {
    PanelPreviewAction action{};
    const auto text = [&localizer = localizer_](const px::ui::TextId id) { return localizer.Text(id); };
    ImGui::BeginChild("NetworkPage", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_Borders);
    const auto buttonWidth = [&text](const px::ui::TextId id) {
        return ImGui::CalcTextSize(text(id).data()).x + ImGui::GetStyle().FramePadding.x * 2.0F;
    };
    const float toolbarWidth{buttonWidth(px::ui::TextId::SimplifiedChinese) + buttonWidth(px::ui::TextId::English) +
                             buttonWidth(px::ui::TextId::DarkTheme) + buttonWidth(px::ui::TextId::LightTheme) +
                             ImGui::GetStyle().ItemSpacing.x * 3.0F};
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - toolbarWidth - ImGui::GetStyle().WindowPadding.x));
    if (ImGui::SmallButton(text(px::ui::TextId::SimplifiedChinese).data())) {
        localizer_.SetLanguage(px::ui::Language::SimplifiedChinese);
        settingsPort_->SetLanguage(px::ui::Language::SimplifiedChinese);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(text(px::ui::TextId::English).data())) {
        localizer_.SetLanguage(px::ui::Language::English);
        settingsPort_->SetLanguage(px::ui::Language::English);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(text(px::ui::TextId::DarkTheme).data())) {
        theme_ = px::ui::Theme::Dark;
        action.selectedTheme = theme_;
        settingsPort_->SetTheme(theme_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(text(px::ui::TextId::LightTheme).data())) {
        theme_ = px::ui::Theme::Light;
        action.selectedTheme = theme_;
        settingsPort_->SetTheme(theme_);
    }

    settings_.Draw(localizer_);
    ImGui::EndChild();
    return action;
}

PanelPreviewAction PanelPreview::Draw() {
    const NavigationAction navigationAction{navigation_.Draw(localizer_)};
    ImGui::SameLine();
    if (navigationAction.selectedPage == PanelPage::Settings) {
        auto action = DrawSettingsPage();
        if (initialThemePending_) {
            action.selectedTheme = theme_;
            initialThemePending_ = false;
        }
        action.exitRequested = navigationAction.exitRequested;
        notifications_->Draw();
        if (voiceCallConsent_) voiceCallConsent_->Draw(localizer_);
        return action;
    }
    ImGui::BeginChild("PageContent", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_Borders);
    if (navigationAction.selectedPage == PanelPage::RemoteControl) {
        remoteControl_.Draw(localizer_);
    } else if (navigationAction.selectedPage == PanelPage::CloudApplications) {
        cloudApplications_.Draw(localizer_);
    } else if (navigationAction.selectedPage == PanelPage::ServerStatus) {
        serverStatus_.Draw(localizer_);
    } else if (navigationAction.selectedPage == PanelPage::Security) {
        securityRecords_.Draw(localizer_);
    }
    ImGui::EndChild();
    notifications_->Draw();
    if (voiceCallConsent_) voiceCallConsent_->Draw(localizer_);
    PanelPreviewAction action{.exitRequested = navigationAction.exitRequested};
    if (initialThemePending_) {
        action.selectedTheme = theme_;
        initialThemePending_ = false;
    }
    return action;
}

} // namespace px::panel::ui
