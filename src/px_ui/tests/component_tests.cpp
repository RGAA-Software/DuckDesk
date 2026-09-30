#include <imgui.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/feedback.h"
#include "px_ui/components/form.h"
#include "px_ui/components/navigation.h"
#include "px_ui/device_platform.h"
#include "px_ui/px_ui_theme.h"
#include "px_ui/theme_tokens.h"

namespace {

bool Equal(const float left, const float right) noexcept { return std::abs(left - right) < 0.0001F; }

bool FieldActionsHaveMatchingBounds(const px::ui::Theme theme, const float scale) {
    px::ui::ApplyPixelsTheme(theme, scale);
    ImGui::NewFrame();
    ImGui::SetNextWindowSize({700.0F, 240.0F});
    ImGui::Begin("Field action geometry", nullptr, ImGuiWindowFlags_NoSavedSettings);
    std::string targetAddress{"123456789"};
    static_cast<void>(px::ui::TextField({"target-address"}, targetAddress, {}, {.width = 200.0F}));
    const ImVec2 fieldMinimum{ImGui::GetItemRectMin()};
    const ImVec2 fieldMaximum{ImGui::GetItemRectMax()};
    ImGui::SameLine();
    static_cast<void>(px::ui::ActionButton({"connect-target"}, "Connect", {.icon = px::ui::VectorIcon::Connect, .width = 120.0F}));
    const ImVec2 buttonMinimum{ImGui::GetItemRectMin()};
    const ImVec2 buttonMaximum{ImGui::GetItemRectMax()};
    ImGui::SameLine();
    static_cast<void>(px::ui::IconAction({"copy-target"}, px::ui::VectorIcon::Copy, {}));
    const ImVec2 iconMinimum{ImGui::GetItemRectMin()};
    const ImVec2 iconMaximum{ImGui::GetItemRectMax()};
    ImGui::End();
    ImGui::EndFrame();
    return Equal(fieldMaximum.y - fieldMinimum.y, px::ui::MetricsFor(scale).controlDefault) && Equal(fieldMinimum.y, buttonMinimum.y) &&
           Equal(fieldMaximum.y, buttonMaximum.y) && Equal(fieldMinimum.y, iconMinimum.y) && Equal(fieldMaximum.y, iconMaximum.y);
}

bool StandardControlsHaveMatchingMetrics(const px::ui::Theme theme, const float scale) {
    px::ui::ApplyPixelsTheme(theme, scale);
    const auto metrics{px::ui::MetricsFor(scale)};
    ImGui::NewFrame();
    ImGui::SetNextWindowSize({900.0F, 700.0F});
    ImGui::Begin("Standard control geometry", nullptr, ImGuiWindowFlags_NoSavedSettings);
    bool matches{true};
    const auto checkBounds = [&matches](const std::string_view controlName, const float expectedHeight, const float expectedWidth = 0.0F) {
        const ImVec2 actualSize{ImGui::GetItemRectSize()};
        if (!Equal(actualSize.y, expectedHeight) || (expectedWidth > 0.0F && !Equal(actualSize.x, expectedWidth))) {
            std::printf("%.*s: %.2f x %.2f, expected %.2f x %.2f\n", static_cast<int>(controlName.size()), controlName.data(), actualSize.x,
                        actualSize.y, expectedWidth, expectedHeight);
            matches = false;
        }
    };
    int numberValue{1920};
    static_cast<void>(px::ui::NumberField({"resolution-width"}, numberValue, 1, 10, {.width = 88.0F * scale}));
    checkBounds("NumberField", metrics.controlDefault, 88.0F * scale);
    const std::array options{px::ui::SelectOption{2, "Two"}, px::ui::SelectOption{4, "Four"}};
    int selectedValue{2};
    static_cast<void>(px::ui::SelectField({"quality"}, selectedValue, options, 200.0F * scale));
    checkBounds("SelectField", metrics.controlDefault);
    static_cast<void>(px::ui::SliderIntField({"bitrate"}, numberValue, 1, 5000, 200.0F * scale));
    checkBounds("SliderIntField", metrics.controlDefault);
    bool checked{true};
    static_cast<void>(px::ui::CheckboxField({"remember-device"}, "Remember", checked));
    checkBounds("CheckboxField", metrics.controlDefault);
    static_cast<void>(px::ui::ToggleSwitch({"allow-control"}, {}, checked));
    checkBounds("ToggleSwitch", metrics.controlDefault, metrics.switchWidth);
    static_cast<void>(px::ui::SegmentedControl({"theme-selection"}, selectedValue, options));
    checkBounds("SegmentedControl", metrics.controlDefault);
    std::string password{"PASSWORD"};
    static_cast<void>(px::ui::PasswordField({"disabled-password"}, password, {}, {.width = 240.0F * scale, .disabled = true}));
    checkBounds("Password visibility action", metrics.controlDefault);
    matches = matches && (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    static_cast<void>(px::ui::ActionButton({"full-width-action"}, "Connect", {.width = -1.0F}));
    checkBounds("Full-width action", metrics.controlDefault);
    matches = matches && ImGui::GetItemRectSize().x > 0.0F;
    const std::string clipped{px::ui::EllipsizedText("Long device name", 40.0F * scale)};
    matches = matches && clipped != "Long device name" && ImGui::CalcTextSize(clipped.c_str()).x <= 40.0F * scale;
    ImGui::End();
    ImGui::EndFrame();
    return matches;
}

bool DropdownCanOpenAndClose(const px::ui::Theme theme, const float scale) {
    px::ui::ApplyPixelsTheme(theme, scale);
    const std::array options{px::ui::SelectOption{2, "Two"}, px::ui::SelectOption{4, "Four"}};
    int selectedValue{2};
    ImVec2 fieldCenter{};
    bool opened{};
    bool closed{};
    for (int frameIndex{}; frameIndex < 6; ++frameIndex) {
        if (frameIndex == 1) {
            ImGui::GetIO().AddMousePosEvent(fieldCenter.x, fieldCenter.y);
        } else if (frameIndex == 2) {
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        } else if (frameIndex == 3) {
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        } else if (frameIndex == 4) {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        } else if (frameIndex == 5) {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        }
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0F, 0.0F});
        ImGui::SetNextWindowSize({600.0F, 400.0F});
        ImGui::Begin("Dropdown interaction", nullptr, ImGuiWindowFlags_NoSavedSettings);
        static_cast<void>(px::ui::SelectField({"interactive-quality"}, selectedValue, options, 200.0F * scale));
        if (frameIndex == 0) {
            const ImVec2 minimum{ImGui::GetItemRectMin()};
            const ImVec2 maximum{ImGui::GetItemRectMax()};
            fieldCenter = {(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F};
        }
        const bool popupOpen{ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)};
        if (frameIndex == 3) opened = popupOpen;
        if (frameIndex == 5) closed = !popupOpen;
        ImGui::End();
        ImGui::EndFrame();
    }
    if (!opened || !closed) std::printf("Dropdown theme %d scale %.2f: opened=%d closed=%d\n", static_cast<int>(theme), scale, opened, closed);
    return opened && closed;
}

}  // namespace

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
    if (!Equal(base.controlXs, 24.0F) || !Equal(base.controlSm, 24.0F) || !Equal(base.controlDefault, 32.0F) || !Equal(base.controlLg, 40.0F)) {
        return 9;
    }
    if (!(dark.background.x > 0.08F && dark.card.x > dark.background.x && dark.popover.x > dark.card.x)) {
        return 10;
    }
    if (!Equal(doubled.controlDefault, base.controlDefault * 2.0F) || !Equal(doubled.cardRadius, base.cardRadius * 2.0F)) {
        return 2;
    }

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = {1280.0F, 720.0F};
    ImGui::GetIO().DeltaTime = 1.0F / 60.0F;
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().ConfigErrorRecoveryEnableAssert = true;
    ImFontConfig fontConfiguration{};
    fontConfiguration.SizePixels = base.fontSize;
    ImGui::GetIO().Fonts->AddFontDefault(&fontConfiguration);
    px::ui::ApplyPixelsTheme(px::ui::Theme::Dark, 1.5F);
    const ImVec2 padding{ImGui::GetStyle().WindowPadding};
    px::ui::ApplyPixelsTheme(px::ui::Theme::Dark, 1.5F);
    if (!Equal(padding.x, ImGui::GetStyle().WindowPadding.x) || !Equal(padding.y, ImGui::GetStyle().WindowPadding.y) ||
        !Equal(ImGui::GetStyle().WindowRounding, 0.0F) || !Equal(ImGui::GetStyle().ScrollbarPadding, 7.0F) ||
        !Equal(ImGui::GetStyle().ScrollbarSize, 24.0F) || !Equal(ImGui::GetStyle().Colors[ImGuiCol_ScrollbarBg].w, 0.0F)) {
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
        !Equal(enhancedDim.w, 0.64F) || ImGui::GetStyle().Colors[ImGuiCol_NavCursor].w <= 0.0F) {
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
    for (const auto theme : {px::ui::Theme::Light, px::ui::Theme::Dark}) {
        for (const float scale : {1.0F, 1.25F, 1.5F, 2.0F}) {
            if (!FieldActionsHaveMatchingBounds(theme, scale) || !StandardControlsHaveMatchingMetrics(theme, scale) ||
                !DropdownCanOpenAndClose(theme, scale)) {
                ImGui::DestroyContext();
                return 11;
            }
        }
    }
    ImGui::DestroyContext();
    return 0;
}
