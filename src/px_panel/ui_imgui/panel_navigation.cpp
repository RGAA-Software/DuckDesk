#include "panel_navigation.h"

#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <array>
#include <string_view>

namespace px::panel::ui {
namespace {

struct NavigationItem final {
    PanelPage page{};
    px::ui::TextId text{};
};

constexpr std::array kNavigationItems{
    NavigationItem{PanelPage::RemoteControl, px::ui::TextId::RemoteControl},
    NavigationItem{PanelPage::CloudApplications, px::ui::TextId::CloudApplications},
    NavigationItem{PanelPage::ServerStatus, px::ui::TextId::ServerStatus},
    NavigationItem{PanelPage::Security, px::ui::TextId::Security},
    NavigationItem{PanelPage::Settings, px::ui::TextId::Settings},
    NavigationItem{PanelPage::Hardware, px::ui::TextId::Hardware},
};

void DrawDisabledText(const std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
}

} // namespace

NavigationAction PanelNavigation::Draw(const px::ui::Localizer& localizer) {
    NavigationAction action{.selectedPage = selectedPage_};
    ImGui::BeginChild("Navigation", ImVec2{px::ui::Scale(224.0F), 0.0F}, ImGuiChildFlags_Borders);
    ImGui::TextColored(ImVec4{0.35F, 0.68F, 1.00F, 1.00F}, "PIXELS");
    DrawDisabledText(localizer.Text(px::ui::TextId::RenderNodeConsole));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const ImVec2 buttonSize{-1.0F, px::ui::Scale(42.0F)};
    for (const auto& item : kNavigationItems) {
        const bool wasSelected{item.page == selectedPage_};
        if (wasSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.12F, 0.36F, 0.82F, 1.00F});
        }
        if (ImGui::Button(localizer.Text(item.text).data(), buttonSize)) {
            selectedPage_ = item.page;
        }
        if (wasSelected) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - px::ui::Scale(58.0F));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.72F, 0.12F, 0.18F, 1.00F});
    action.exitRequested = ImGui::Button(localizer.Text(px::ui::TextId::ExitPrograms).data(), buttonSize);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    action.selectedPage = selectedPage_;
    return action;
}

PanelPage PanelNavigation::SelectedPage() const noexcept {
    return selectedPage_;
}

} // namespace px::panel::ui
