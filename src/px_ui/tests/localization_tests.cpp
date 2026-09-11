#include "px_ui/localization.h"
#include "px_ui/px_ui_theme.h"

#include <imgui.h>

int main() {
    if (!px::ui::CatalogsAreComplete()) {
        return 1;
    }

    px::ui::Localizer localizer{};
    if (localizer.Text(px::ui::TextId::Settings).empty()) {
        return 2;
    }
    localizer.SetLanguage(px::ui::Language::English);
    if (localizer.Text(px::ui::TextId::Settings) != "Settings") {
        return 3;
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
    ImGui::DestroyContext();
    if (darkBackground.x == lightBackground.x && darkBackground.y == lightBackground.y && darkBackground.z == lightBackground.z) {
        return 5;
    }
    return 0;
}
