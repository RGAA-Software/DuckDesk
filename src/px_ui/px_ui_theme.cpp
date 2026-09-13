#include "px_ui/px_ui_theme.h"

#include "px_ui/theme_tokens.h"

#include <imgui.h>

namespace px::ui {

void ApplyPixelsColors(const Theme theme) {
    ImGuiStyle& style = ImGui::GetStyle();
    const ThemeTokens tokens{ThemeTokensFor(theme)};
    style.Colors[ImGuiCol_Text] = tokens.foreground;
    style.Colors[ImGuiCol_TextDisabled] = tokens.mutedForeground;
    style.Colors[ImGuiCol_WindowBg] = tokens.background;
    style.Colors[ImGuiCol_ChildBg] = tokens.background;
    style.Colors[ImGuiCol_PopupBg] = tokens.popover;
    style.Colors[ImGuiCol_Border] = tokens.border;
    style.Colors[ImGuiCol_BorderShadow] = ImVec4{};
    style.Colors[ImGuiCol_FrameBg] = tokens.background;
    style.Colors[ImGuiCol_FrameBgHovered] = tokens.muted;
    style.Colors[ImGuiCol_FrameBgActive] = tokens.accent;
    style.Colors[ImGuiCol_Button] = tokens.primary;
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4{tokens.primary.x, tokens.primary.y, tokens.primary.z, 0.90F};
    style.Colors[ImGuiCol_ButtonActive] = ImVec4{tokens.primary.x * 0.88F, tokens.primary.y * 0.88F, tokens.primary.z * 0.88F, 1.0F};
    style.Colors[ImGuiCol_Header] = tokens.accent;
    style.Colors[ImGuiCol_HeaderHovered] = tokens.muted;
    style.Colors[ImGuiCol_HeaderActive] = tokens.accent;
    style.Colors[ImGuiCol_CheckMark] = tokens.primary;
    style.Colors[ImGuiCol_Separator] = tokens.border;
    style.Colors[ImGuiCol_ResizeGrip] = tokens.border;
    style.Colors[ImGuiCol_Tab] = tokens.background;
    style.Colors[ImGuiCol_TabHovered] = tokens.muted;
    style.Colors[ImGuiCol_TabSelected] = tokens.accent;
    style.Colors[ImGuiCol_NavCursor] = tokens.ring;
}

void ApplyPixelsTheme(const Theme theme, const float scale, const bool enhancedVisualEffects) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle{};
    style.WindowPadding = ImVec2{16.0F, 16.0F};
    style.FramePadding = ImVec2{10.0F, 6.0F};
    style.ItemSpacing = ImVec2{8.0F, 8.0F};
    style.ItemInnerSpacing = ImVec2{6.0F, 4.0F};
    style.WindowRounding = 0.0F;
    style.ChildRounding = 10.0F;
    style.FrameRounding = 6.0F;
    style.PopupRounding = 10.0F;
    style.ScrollbarRounding = 6.0F;
    style.GrabRounding = 6.0F;
    style.FrameBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.ScrollbarSize = 10.0F;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    ApplyPixelsColors(theme);
    style.Colors[ImGuiCol_PopupBg].w = enhancedVisualEffects ? 0.94F : 1.0F;
    style.Colors[ImGuiCol_ModalWindowDimBg].w = enhancedVisualEffects ? 0.52F : 0.46F;
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
}

bool EnhancedVisualEffectsEnabled() noexcept {
    return ImGui::GetStyle().Colors[ImGuiCol_PopupBg].w < 0.99F;
}

} // namespace px::ui
