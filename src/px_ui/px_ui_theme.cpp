#include "px_ui/px_ui_theme.h"

#include <imgui.h>

namespace px::ui {
namespace {

void ApplyDarkColors(ImGuiStyle& style) {
    style.Colors[ImGuiCol_Text] = ImVec4{0.91F, 0.93F, 0.97F, 1.00F};
    style.Colors[ImGuiCol_TextDisabled] = ImVec4{0.48F, 0.53F, 0.62F, 1.00F};
    style.Colors[ImGuiCol_WindowBg] = ImVec4{0.055F, 0.067F, 0.094F, 1.00F};
    style.Colors[ImGuiCol_ChildBg] = ImVec4{0.075F, 0.090F, 0.125F, 1.00F};
    style.Colors[ImGuiCol_PopupBg] = ImVec4{0.075F, 0.090F, 0.125F, 0.98F};
    style.Colors[ImGuiCol_Border] = ImVec4{0.17F, 0.20F, 0.27F, 1.00F};
    style.Colors[ImGuiCol_FrameBg] = ImVec4{0.105F, 0.125F, 0.170F, 1.00F};
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4{0.14F, 0.18F, 0.26F, 1.00F};
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4{0.16F, 0.22F, 0.34F, 1.00F};
}

void ApplyLightColors(ImGuiStyle& style) {
    style.Colors[ImGuiCol_Text] = ImVec4{0.10F, 0.13F, 0.18F, 1.00F};
    style.Colors[ImGuiCol_TextDisabled] = ImVec4{0.43F, 0.48F, 0.56F, 1.00F};
    style.Colors[ImGuiCol_WindowBg] = ImVec4{0.96F, 0.97F, 0.985F, 1.00F};
    style.Colors[ImGuiCol_ChildBg] = ImVec4{0.99F, 0.995F, 1.00F, 1.00F};
    style.Colors[ImGuiCol_PopupBg] = ImVec4{0.99F, 0.995F, 1.00F, 0.98F};
    style.Colors[ImGuiCol_Border] = ImVec4{0.79F, 0.82F, 0.87F, 1.00F};
    style.Colors[ImGuiCol_FrameBg] = ImVec4{0.91F, 0.93F, 0.96F, 1.00F};
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4{0.84F, 0.89F, 0.97F, 1.00F};
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4{0.76F, 0.84F, 0.96F, 1.00F};
}

} // namespace

void ApplyPixelsColors(const Theme theme) {
    ImGuiStyle& style = ImGui::GetStyle();
    if (theme == Theme::Dark) {
        ApplyDarkColors(style);
    } else {
        ApplyLightColors(style);
    }
    style.Colors[ImGuiCol_Button] = ImVec4{0.12F, 0.36F, 0.82F, 1.00F};
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4{0.16F, 0.43F, 0.94F, 1.00F};
    style.Colors[ImGuiCol_ButtonActive] = ImVec4{0.10F, 0.30F, 0.72F, 1.00F};
    style.Colors[ImGuiCol_Header] = ImVec4{0.12F, 0.36F, 0.82F, 0.72F};
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4{0.16F, 0.43F, 0.94F, 0.82F};
    style.Colors[ImGuiCol_HeaderActive] = ImVec4{0.10F, 0.30F, 0.72F, 1.00F};
    style.Colors[ImGuiCol_CheckMark] = ImVec4{0.35F, 0.68F, 1.00F, 1.00F};
}

void ApplyPixelsTheme(const Theme theme, const float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle{};
    style.WindowPadding = ImVec2{20.0F, 18.0F};
    style.FramePadding = ImVec2{12.0F, 8.0F};
    style.ItemSpacing = ImVec2{10.0F, 10.0F};
    style.WindowRounding = 10.0F;
    style.ChildRounding = 10.0F;
    style.FrameRounding = 7.0F;
    style.PopupRounding = 9.0F;
    style.ScrollbarRounding = 8.0F;
    style.GrabRounding = 7.0F;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    ApplyPixelsColors(theme);
}

} // namespace px::ui
