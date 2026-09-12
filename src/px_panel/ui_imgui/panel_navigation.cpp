#include "panel_navigation.h"

#include "px_ui/layout_metrics.h"
#include "px_ui/vector_icon.h"

#include <imgui.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace px::panel::ui {
namespace {

struct NavigationItem final {
    PanelPage page{};
    px::ui::TextId text{};
    px::ui::VectorIcon icon{};
};

constexpr std::array kNavigationItems{
    NavigationItem{PanelPage::RemoteControl, px::ui::TextId::RemoteControl, px::ui::VectorIcon::Monitor},
    NavigationItem{PanelPage::DeviceList, px::ui::TextId::DeviceList, px::ui::VectorIcon::List},
    NavigationItem{PanelPage::CloudApplications, px::ui::TextId::CloudApplications, px::ui::VectorIcon::Cloud},
    NavigationItem{PanelPage::ServerStatus, px::ui::TextId::ServerStatus, px::ui::VectorIcon::Activity},
    NavigationItem{PanelPage::Security, px::ui::TextId::Security, px::ui::VectorIcon::Shield},
    NavigationItem{PanelPage::Settings, px::ui::TextId::Settings, px::ui::VectorIcon::Settings},
};

} // namespace

PanelNavigation::PanelNavigation(std::shared_ptr<AccountPort> accountPort) : account_{std::move(accountPort)} {}

NavigationAction PanelNavigation::Draw(const px::ui::Localizer& localizer) {
    NavigationAction action{.selectedPage = selectedPage_};
    constexpr ImGuiWindowFlags navigationFlags{ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};
    ImGui::BeginChild("Navigation", ImVec2{px::ui::Scale(224.0F), 0.0F}, ImGuiChildFlags_Borders, navigationFlags);
    account_.Draw(localizer);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const ImVec2 buttonSize{-1.0F, px::ui::Scale(42.0F)};
    for (const auto& item : kNavigationItems) {
        const bool wasSelected{item.page == selectedPage_};
        const ImVec4 buttonColor{wasSelected ? ImVec4{0.12F, 0.36F, 0.82F, 1.00F} : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)};
        const ImVec4 hoverColor{wasSelected ? ImVec4{0.16F, 0.43F, 0.94F, 1.00F} : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered)};
        const ImVec4 activeColor{wasSelected ? ImVec4{0.10F, 0.30F, 0.72F, 1.00F} : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive)};
        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
        const std::string id{"navigation-" + std::to_string(static_cast<int>(item.page))};
        if (px::ui::IconButton(item.icon, localizer.Text(item.text), id, buttonSize)) {
            selectedPage_ = item.page;
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - px::ui::Scale(58.0F));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.72F, 0.12F, 0.18F, 1.00F});
    action.exitRequested = px::ui::IconButton(px::ui::VectorIcon::LogOut, localizer.Text(px::ui::TextId::ExitPrograms), "exit-programs", buttonSize);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    action.selectedPage = selectedPage_;
    return action;
}

PanelPage PanelNavigation::SelectedPage() const noexcept {
    return selectedPage_;
}

} // namespace px::panel::ui
