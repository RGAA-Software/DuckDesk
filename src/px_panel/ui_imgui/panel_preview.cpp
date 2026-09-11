#include "panel_preview.h"

#include <imgui.h>

#include <string_view>

namespace px::panel::ui {
namespace {

void DrawText(const std::string_view text) {
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

void DrawDisabledText(const std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    DrawText(text);
    ImGui::PopStyleColor();
}

void DrawEndpoint(const std::string_view label, const std::string_view value, const std::string_view purpose) {
    DrawDisabledText(label);
    ImGui::SameLine(190.0F);
    DrawText(value);
    ImGui::SameLine(430.0F);
    DrawDisabledText(purpose);
}

} // namespace

void PanelPreview::DrawNavigation() {
    const auto text = [&localizer = localizer_](const px::ui::TextId id) { return localizer.Text(id); };
    ImGui::BeginChild("Navigation", ImVec2{224.0F, 0.0F}, ImGuiChildFlags_Borders);
    ImGui::TextColored(ImVec4{0.35F, 0.68F, 1.00F, 1.00F}, "PIXELS");
    DrawDisabledText(text(px::ui::TextId::RenderNodeConsole));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    constexpr ImVec2 navigationButtonSize{-1.0F, 42.0F};
    ImGui::Button(text(px::ui::TextId::RemoteControl).data(), navigationButtonSize);
    ImGui::Button(text(px::ui::TextId::CloudApplications).data(), navigationButtonSize);
    ImGui::Button(text(px::ui::TextId::ServerStatus).data(), navigationButtonSize);
    ImGui::Button(text(px::ui::TextId::Security).data(), navigationButtonSize);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.12F, 0.36F, 0.82F, 1.00F});
    ImGui::Button(text(px::ui::TextId::Settings).data(), navigationButtonSize);
    ImGui::PopStyleColor();
    ImGui::Button(text(px::ui::TextId::Hardware).data(), navigationButtonSize);

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 58.0F);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.72F, 0.12F, 0.18F, 1.00F});
    ImGui::Button(text(px::ui::TextId::ExitPrograms).data(), navigationButtonSize);
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void PanelPreview::DrawNetworkPage() {
    const auto text = [&localizer = localizer_](const px::ui::TextId id) { return localizer.Text(id); };
    ImGui::BeginChild("NetworkPage", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_Borders);
    DrawText(text(px::ui::TextId::SettingsNetwork));
    DrawDisabledText(text(px::ui::TextId::ConnectionAddresses));
    ImGui::SameLine(ImGui::GetWindowWidth() - 205.0F);
    if (ImGui::SmallButton(text(px::ui::TextId::SimplifiedChinese).data())) {
        localizer_.SetLanguage(px::ui::Language::SimplifiedChinese);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(text(px::ui::TextId::English).data())) {
        localizer_.SetLanguage(px::ui::Language::English);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(text(px::ui::TextId::DarkTheme).data())) {
        theme_ = px::ui::Theme::Dark;
        px::ui::ApplyPixelsColors(theme_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(text(px::ui::TextId::LightTheme).data())) {
        theme_ = px::ui::Theme::Light;
        px::ui::ApplyPixelsColors(theme_);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawText(text(px::ui::TextId::Authorization));
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextMultiline("##authorization", authorizationInfo_.data(), authorizationInfo_.size(), ImVec2{-1.0F, 92.0F});

    ImGui::Spacing();
    DrawText(text(px::ui::TextId::ResolvedControlEndpoints));
    ImGui::BeginChild("ResolvedEndpoints", ImVec2{0.0F, 108.0F}, ImGuiChildFlags_Borders);
    DrawEndpoint(text(px::ui::TextId::Supervisor), "--", text(px::ui::TextId::NodeManagement));
    DrawEndpoint(text(px::ui::TextId::Relay), "--", text(px::ui::TextId::ReliableRoutedConnection));
    ImGui::EndChild();

    ImGui::Spacing();
    DrawText(text(px::ui::TextId::NodePublicAddress));
    DrawDisabledText(text(px::ui::TextId::OptionalPublicAddress));
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##publicAddress", text(px::ui::TextId::PublicAddressHint).data(), publicAddress_.data(), publicAddress_.size());

    ImGui::Spacing();
    DrawText(text(px::ui::TextId::NodeListeningPorts));
    ImGui::BeginChild("NodePorts", ImVec2{0.0F, 138.0F}, ImGuiChildFlags_Borders);
    DrawEndpoint(text(px::ui::TextId::ServiceManagementPort), "4603/TCP", text(px::ui::TextId::ServiceManagementPurpose));
    DrawEndpoint(text(px::ui::TextId::DesktopConnectionPort), "4601/TCP", text(px::ui::TextId::DesktopConnectionPurpose));
    DrawEndpoint(text(px::ui::TextId::ApplicationPortPool), "4613-4998", text(px::ui::TextId::ApplicationPortPurpose));
    DrawEndpoint(text(px::ui::TextId::RtcMediaPool), "5000-5031", text(px::ui::TextId::RtcPortPurpose));
    DrawEndpoint(text(px::ui::TextId::PanelListeningPort), "4999/TCP", text(px::ui::TextId::PanelListeningPurpose));
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button(text(px::ui::TextId::Save).data(), ImVec2{150.0F, 40.0F})) {
        status_ = px::ui::TextId::PreviewSavedStatus;
    }
    ImGui::SameLine();
    DrawDisabledText(text(status_));
    ImGui::EndChild();
}

void PanelPreview::Draw() {
    DrawNavigation();
    ImGui::SameLine();
    DrawNetworkPage();
}

} // namespace px::panel::ui
