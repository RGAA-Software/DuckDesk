#include "px_ui/px_ui_theme.h"

#include <imgui.h>

#include "px_ui/theme_tokens.h"

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
    style.Colors[ImGuiCol_FrameBg] = tokens.secondary;
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
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4{};
    style.Colors[ImGuiCol_ResizeGrip] = tokens.border;
    style.Colors[ImGuiCol_Tab] = tokens.card;
    style.Colors[ImGuiCol_TabHovered] = tokens.muted;
    style.Colors[ImGuiCol_TabSelected] = tokens.accent;
    style.Colors[ImGuiCol_NavCursor] = tokens.ring;
    style.Colors[ImGuiCol_TableHeaderBg] = tokens.secondary;
    style.Colors[ImGuiCol_TableBorderStrong] = tokens.border;
    style.Colors[ImGuiCol_TableBorderLight] = tokens.border;
    style.Colors[ImGuiCol_TableRowBg] = ImVec4{};
    style.Colors[ImGuiCol_TableRowBgAlt] = {tokens.muted.x, tokens.muted.y, tokens.muted.z, 0.12F};
}

void ApplyPixelsTheme(const Theme theme, const float scale, const bool enhancedVisualEffects) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle{};
    style.WindowPadding = ImVec2{16.0F, 16.0F};
    style.FramePadding = ImVec2{12.0F, 9.0F};
    style.CellPadding = ImVec2{8.0F, 4.0F};
    style.ItemSpacing = ImVec2{8.0F, 8.0F};
    style.ItemInnerSpacing = ImVec2{6.0F, 4.0F};
    style.WindowRounding = 0.0F;
    style.ChildRounding = 8.0F;
    style.FrameRounding = 6.0F;
    style.PopupRounding = 8.0F;
    style.ScrollbarRounding = 6.0F;
    style.ScrollbarPadding = 5.0F;
    style.GrabRounding = 6.0F;
    style.FrameBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.ScrollbarSize = 16.0F;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    ApplyPixelsColors(theme);
    style.Colors[ImGuiCol_PopupBg].w = enhancedVisualEffects ? 0.94F : 1.0F;
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4{0.0F, 0.0F, 0.0F, enhancedVisualEffects ? 0.64F : 0.56F};
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
}

bool EnhancedVisualEffectsEnabled() noexcept { return ImGui::GetStyle().Colors[ImGuiCol_PopupBg].w < 0.99F; }

}  // namespace px::ui
