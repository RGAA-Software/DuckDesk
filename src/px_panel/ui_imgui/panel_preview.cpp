#include "panel_preview.h"

#include <imgui.h>

#include <string_view>

namespace px::panel::ui {
namespace {

void DrawDisabledText(const std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
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
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 305.0F);
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

    if (networkPage_.Draw(localizer_) == NetworkPageAction::SaveRequested) {
        networkPage_.SetStatus(px::ui::TextId::PreviewSavedStatus);
    }
    ImGui::EndChild();
}

void PanelPreview::Draw() {
    DrawNavigation();
    ImGui::SameLine();
    DrawNetworkPage();
}

} // namespace px::panel::ui
