#include "panel_preview.h"
#include "panel_layout.h"

#include "px_ui/components/navigation.h"
#include "px_ui/components/surface.h"
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
    : settingsPort_{services.settings}, notifications_{std::move(services.notifications)}, voiceCallConsent_{std::move(services.voiceCallConsent)},
      navigation_{std::move(services.account)}, settings_{std::move(services.networkSettings), services.settings},
      serverStatus_{std::move(services.serverStatus)}, remoteControl_{services.remoteControl}, deviceList_{std::move(services.remoteControl)},
      cloudApplications_{std::move(services.cloudApplications)}, securityRecords_{std::move(services.securityRecords)} {
    const auto appearance = settingsPort_->Snapshot();
    localizer_.SetLanguage(appearance.language);
    theme_ = appearance.theme;
    enhancedVisualEffects_ = appearance.enhancedVisualEffects;
}

PanelPreviewAction PanelPreview::DrawSettingsPage() {
    PanelPreviewAction action{};
    const auto text = [&localizer = localizer_](const px::ui::TextId id) { return localizer.Text(id); };
    ImGui::BeginChild("SettingsPage", ImVec2{-px::ui::Scale(10.0F), 0.0F}, ImGuiChildFlags_None);
    px::ui::PageTitle(text(px::ui::TextId::Settings));
    const auto buttonWidth = [&text](const px::ui::TextId id) { return ImGui::CalcTextSize(text(id).data()).x + px::ui::Scale(16.0F); };
    const float toolbarWidth{buttonWidth(px::ui::TextId::SimplifiedChinese) + buttonWidth(px::ui::TextId::English) +
                             buttonWidth(px::ui::TextId::DarkTheme) + buttonWidth(px::ui::TextId::LightTheme) +
                             ImGui::GetStyle().ItemSpacing.x * 3.0F};
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - toolbarWidth));
    if (px::ui::SegmentedItem({"language-zh-cn"}, text(px::ui::TextId::SimplifiedChinese),
                              localizer_.CurrentLanguage() == px::ui::Language::SimplifiedChinese, 0.0F, px::ui::WidgetSize::Xs)) {
        localizer_.SetLanguage(px::ui::Language::SimplifiedChinese);
        settingsPort_->SetLanguage(px::ui::Language::SimplifiedChinese);
    }
    ImGui::SameLine();
    if (px::ui::SegmentedItem({"language-en"}, text(px::ui::TextId::English), localizer_.CurrentLanguage() == px::ui::Language::English, 0.0F,
                              px::ui::WidgetSize::Xs)) {
        localizer_.SetLanguage(px::ui::Language::English);
        settingsPort_->SetLanguage(px::ui::Language::English);
    }
    ImGui::SameLine();
    if (px::ui::SegmentedItem({"theme-dark"}, text(px::ui::TextId::DarkTheme), theme_ == px::ui::Theme::Dark, 0.0F, px::ui::WidgetSize::Xs)) {
        theme_ = px::ui::Theme::Dark;
        action.selectedTheme = theme_;
        settingsPort_->SetTheme(theme_);
    }
    ImGui::SameLine();
    if (px::ui::SegmentedItem({"theme-light"}, text(px::ui::TextId::LightTheme), theme_ == px::ui::Theme::Light, 0.0F, px::ui::WidgetSize::Xs)) {
        theme_ = px::ui::Theme::Light;
        action.selectedTheme = theme_;
        settingsPort_->SetTheme(theme_);
    }
    ImGui::Dummy({0.0F, layout::PageHeaderGap()});
    settings_.Draw(localizer_);
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
        return action;
    }
    ImGui::BeginChild("PageContent", ImVec2{-px::ui::Scale(10.0F), 0.0F}, ImGuiChildFlags_None);
    if (navigationAction.selectedPage == PanelPage::RemoteControl) {
        remoteControl_.Draw(localizer_, platformIcons);
    } else if (navigationAction.selectedPage == PanelPage::DeviceList) {
        deviceList_.Draw(localizer_);
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
