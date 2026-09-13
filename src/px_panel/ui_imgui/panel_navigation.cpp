#include "panel_navigation.h"
#include "panel_layout.h"

#include "px_ui/components/navigation.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/theme_tokens.h"

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
    ImGui::BeginChild("Navigation", ImVec2{layout::NavigationWidth(), 0.0F}, ImGuiChildFlags_None, navigationFlags);
    account_.Draw(localizer);
    ImGui::Spacing();
    const float accountDividerWidth{px::ui::Scale(100.0F)};
    const ImVec2 dividerCursor{ImGui::GetCursorScreenPos()};
    const float accountDividerLeft{dividerCursor.x + (ImGui::GetContentRegionAvail().x - accountDividerWidth) * 0.5F};
    ImGui::GetWindowDrawList()->AddLine({accountDividerLeft, dividerCursor.y}, {accountDividerLeft + accountDividerWidth, dividerCursor.y},
                                        ImGui::GetColorU32(px::ui::CurrentThemeTokens().border));
    ImGui::Dummy({0.0F, px::ui::Scale(1.0F)});
    ImGui::Spacing();

    const float buttonWidth{px::ui::Scale(150.0F)};
    const float buttonHeight{px::ui::Scale(35.0F)};
    const float iconInset{px::ui::Scale(15.0F)};
    const auto centerButton = [buttonWidth] { ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5F); };
    for (const auto& item : kNavigationItems) {
        centerButton();
        const bool wasSelected{item.page == selectedPage_};
        const std::string id{"navigation-" + std::to_string(static_cast<int>(item.page))};
        if (px::ui::NavigationItem({id}, item.icon, localizer.Text(item.text), wasSelected, buttonWidth, px::ui::WidgetSize::Sm, buttonHeight,
                                   iconInset, false)) {
            selectedPage_ = item.page;
        }
    }
    ImGui::EndChild();
    action.selectedPage = selectedPage_;
    return action;
}

PanelPage PanelNavigation::SelectedPage() const noexcept {
    return selectedPage_;
}

} // namespace px::panel::ui
