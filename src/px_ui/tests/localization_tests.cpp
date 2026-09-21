#include <imgui.h>

#include <string>

#include "px_ui/localization.h"
#include "px_ui/product_brand.h"
#include "px_ui/px_ui_theme.h"

namespace {

bool UsesGreenBrandColor(const ImVec4 color) noexcept { return color.y > color.x && color.y > color.z; }

}  // namespace

int main() {
    if (!px::ui::CatalogsAreComplete()) {
        return 1;
    }

    px::ui::Localizer localizer{};
    if (localizer.Text(px::ui::TextId::Settings).empty()) {
        return 2;
    }
    if (localizer.Text(px::ui::TextId::DesktopLink) != "桌面链接") {
        return 7;
    }
    if (localizer.Text(px::ui::TextId::CloudApplications) != "云端应用") {
        return 8;
    }
    if (localizer.Text(px::ui::TextId::ConnectionRemotePreflightUnavailable) !=
        "远程设备不支持连接前置探测，请更新远程设备上的 " + std::string{px::ui::ApplicationName()} + "。") {
        return 10;
    }
    if (localizer.Text(px::ui::TextId::ConnectionRemoteReconnectGrace) != "远程桌面已被占用，请稍后重试。") {
        return 11;
    }
    if (localizer.Text(px::ui::TextId::ConnectionRemoteSessionOccupied) != "远程桌面已被占用，请稍后重试。") {
        return 14;
    }
    localizer.SetLanguage(px::ui::Language::English);
    if (localizer.Text(px::ui::TextId::Settings) != "Settings") {
        return 3;
    }
    if (localizer.Text(px::ui::TextId::CloudApplications) != "Cloud Apps") {
        return 9;
    }
    if (localizer.Text(px::ui::TextId::ConnectionRemotePreflightUnavailable) !=
        "The remote device does not support connection preflight. Update " + std::string{px::ui::ApplicationName()} + " on the remote device.") {
        return 12;
    }
    if (localizer.Text(px::ui::TextId::ConnectionRemoteReconnectGrace) != "The remote desktop is occupied. Please try again shortly.") {
        return 13;
    }
    if (localizer.Text(px::ui::TextId::ConnectionRemoteSessionOccupied) != "The remote desktop is occupied. Please try again shortly.") {
        return 15;
    }

    ImGui::CreateContext();
    px::ui::ApplyPixelsTheme(px::ui::Theme::Dark, 1.25F);
    const ImVec2 firstPadding{ImGui::GetStyle().WindowPadding};
    const ImVec4 darkBackground{ImGui::GetStyle().Colors[ImGuiCol_WindowBg]};
    px::ui::ApplyPixelsTheme(px::ui::Theme::Dark, 1.25F);
    if (ImGui::GetStyle().WindowPadding.x != firstPadding.x || ImGui::GetStyle().WindowPadding.y != firstPadding.y) {
        ImGui::DestroyContext();
        return 4;
    }
    px::ui::ApplyPixelsColors(px::ui::Theme::Light);
    const ImVec4 lightBackground{ImGui::GetStyle().Colors[ImGuiCol_WindowBg]};
    const ImVec4 lightPrimary{ImGui::GetStyle().Colors[ImGuiCol_Button]};
    ImGui::DestroyContext();
    if (darkBackground.x == lightBackground.x && darkBackground.y == lightBackground.y && darkBackground.z == lightBackground.z) {
        return 5;
    }
    if (!UsesGreenBrandColor(lightPrimary)) {
        return 6;
    }
    return 0;
}
