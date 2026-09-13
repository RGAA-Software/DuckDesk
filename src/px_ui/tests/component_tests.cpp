#include "px_ui/components/feedback.h"
#include "px_ui/device_platform.h"
#include "px_ui/px_ui_theme.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <cmath>

namespace {

bool Equal(const float left, const float right) noexcept {
    return std::abs(left - right) < 0.0001F;
}

} // namespace

int main() {
    if (px::ui::ParseDevicePlatform("windows") != px::ui::DevicePlatform::Windows ||
        px::ui::ParseDevicePlatform("darwin") != px::ui::DevicePlatform::MacOS ||
        px::ui::ParseDevicePlatform("android") != px::ui::DevicePlatform::Android ||
        px::ui::ParseDevicePlatform("ios") != px::ui::DevicePlatform::IOS ||
        px::ui::ParseDevicePlatform("unsupported") != px::ui::DevicePlatform::Unknown) {
        return 7;
    }
    const px::ui::ThemeTokens dark{px::ui::ThemeTokensFor(px::ui::Theme::Dark)};
    const px::ui::ThemeTokens light{px::ui::ThemeTokensFor(px::ui::Theme::Light)};
    if (dark.background.x == light.background.x || dark.foreground.x == light.foreground.x) {
        return 1;
    }
    const px::ui::UiMetrics base{px::ui::MetricsFor(1.0F)};
    const px::ui::UiMetrics doubled{px::ui::MetricsFor(2.0F)};
    if (!Equal(doubled.controlDefault, base.controlDefault * 2.0F) || !Equal(doubled.cardRadius, base.cardRadius * 2.0F)) {
        return 2;
    }

    ImGui::CreateContext();
    px::ui::ApplyPixelsTheme(px::ui::Theme::Dark, 1.5F);
    const ImVec2 padding{ImGui::GetStyle().WindowPadding};
    px::ui::ApplyPixelsTheme(px::ui::Theme::Dark, 1.5F);
    if (!Equal(padding.x, ImGui::GetStyle().WindowPadding.x) || !Equal(padding.y, ImGui::GetStyle().WindowPadding.y) ||
        !Equal(ImGui::GetStyle().WindowRounding, 0.0F)) {
        ImGui::DestroyContext();
        return 3;
    }
    px::ui::ApplyPixelsTheme(px::ui::Theme::Dark, 1.5F, false);
    const ImVec4 standardDim{ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg]};
    if (px::ui::EnhancedVisualEffectsEnabled() || !Equal(standardDim.x, 0.0F) || !Equal(standardDim.y, 0.0F) || !Equal(standardDim.z, 0.0F) ||
        !Equal(standardDim.w, 0.56F)) {
        ImGui::DestroyContext();
        return 6;
    }
    px::ui::ApplyPixelsTheme(px::ui::Theme::Light, 1.5F, true);
    const ImVec4 enhancedDim{ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg]};
    if (!px::ui::EnhancedVisualEffectsEnabled() || !Equal(enhancedDim.x, 0.0F) || !Equal(enhancedDim.y, 0.0F) || !Equal(enhancedDim.z, 0.0F) ||
        !Equal(enhancedDim.w, 0.64F) || !Equal(ImGui::GetStyle().Colors[ImGuiCol_NavCursor].w, 0.0F)) {
        ImGui::DestroyContext();
        return 8;
    }
    px::ui::ToastHost host{};
    host.Push({.title = "One"});
    host.Push({.title = "Two"});
    if (host.Size() != 2) {
        ImGui::DestroyContext();
        return 4;
    }
    host.Clear();
    if (host.Size() != 0) {
        ImGui::DestroyContext();
        return 5;
    }
    ImGui::DestroyContext();
    return 0;
}
