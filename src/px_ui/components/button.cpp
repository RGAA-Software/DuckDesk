#include "px_ui/components/button.h"

#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace px::ui {
namespace {

ImVec4 WithAlpha(const ImVec4 color, const float alpha) noexcept {
    return {color.x, color.y, color.z, alpha};
}

ImVec4 Mix(const ImVec4 left, const ImVec4 right, const float amount) noexcept {
    return {left.x + (right.x - left.x) * amount, left.y + (right.y - left.y) * amount, left.z + (right.z - left.z) * amount,
            left.w + (right.w - left.w) * amount};
}

struct ButtonPalette final {
    ImVec4 normal{};
    ImVec4 hovered{};
    ImVec4 active{};
    ImVec4 text{};
    ImVec4 border{};
};

ButtonPalette PaletteFor(const ButtonVariant variant, const ThemeTokens& tokens) noexcept {
    switch (variant) {
    case ButtonVariant::Accent:
        return {tokens.accent, Mix(tokens.accent, tokens.foreground, 0.06F), Mix(tokens.accent, tokens.foreground, 0.10F), tokens.accentForeground,
                tokens.accent};
    case ButtonVariant::Secondary:
        return {tokens.secondary, Mix(tokens.secondary, tokens.foreground, 0.08F), Mix(tokens.secondary, tokens.foreground, 0.14F),
                tokens.secondaryForeground, tokens.secondary};
    case ButtonVariant::Outline:
        return {WithAlpha(tokens.background, 0.0F), tokens.accent, Mix(tokens.accent, tokens.foreground, 0.06F), tokens.foreground, tokens.input};
    case ButtonVariant::Ghost:
        return {WithAlpha(tokens.background, 0.0F), tokens.muted, tokens.accent, tokens.foreground, WithAlpha(tokens.border, 0.0F)};
    case ButtonVariant::Destructive:
        return {tokens.destructive, Mix(tokens.destructive, tokens.foreground, 0.08F), Mix(tokens.destructive, tokens.background, 0.12F),
                tokens.destructiveForeground, tokens.destructive};
    case ButtonVariant::Link:
        return {WithAlpha(tokens.background, 0.0F), WithAlpha(tokens.background, 0.0F), WithAlpha(tokens.background, 0.0F), tokens.primary,
                WithAlpha(tokens.border, 0.0F)};
    case ButtonVariant::Primary:
    default:
        return {tokens.primary, Mix(tokens.primary, tokens.foreground, 0.08F), Mix(tokens.primary, tokens.background, 0.12F),
                tokens.primaryForeground, tokens.primary};
    }
}

float HeightFor(const WidgetSize size, const UiMetrics& metrics) noexcept {
    switch (size) {
    case WidgetSize::Xs:
    case WidgetSize::IconXs:
        return metrics.controlXs;
    case WidgetSize::Sm:
    case WidgetSize::IconSm:
        return metrics.controlSm;
    case WidgetSize::Lg:
    case WidgetSize::IconLg:
        return metrics.controlLg;
    case WidgetSize::Default:
    case WidgetSize::Icon:
    default:
        return metrics.controlDefault;
    }
}

float IconSizeFor(const WidgetSize size, const UiMetrics& metrics) noexcept {
    if (size == WidgetSize::Xs || size == WidgetSize::IconXs) {
        return metrics.iconSm;
    }
    if (size == WidgetSize::Lg || size == WidgetSize::IconLg) {
        return metrics.iconLg;
    }
    return metrics.iconDefault;
}

bool IsIconSize(const WidgetSize size) noexcept {
    return size == WidgetSize::IconXs || size == WidgetSize::IconSm || size == WidgetSize::Icon || size == WidgetSize::IconLg;
}

} // namespace

bool ActionButton(const WidgetId id, const std::string_view label, const ButtonOptions& options) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ButtonPalette palette{PaletteFor(options.variant, tokens)};
    const float height{options.height > 0.0F ? options.height : HeightFor(options.size, metrics)};
    const float iconSize{IconSizeFor(options.size, metrics)};
    const bool iconOnly{IsIconSize(options.size) && options.icon.has_value()};
    const ImVec2 textSize{ImGui::CalcTextSize(label.data(), label.data() + label.size())};
    const float contentWidth{iconOnly ? iconSize : textSize.x + (options.icon.has_value() ? iconSize + metrics.spacingSm : 0.0F)};
    const float automaticWidth{contentWidth + metrics.spacingMd * 2.0F};
    const ImVec2 size{options.width != 0.0F ? options.width : (iconOnly ? height : automaticWidth), height};

    const ScopedId scopedId{id.value};
    const ScopedDisabled disabled{options.disabled || options.busy};
    const ScopedStyleVar rounding{ImGuiStyleVar_FrameRounding, options.circular ? height * 0.5F : metrics.controlRadius};
    const ScopedStyleColor normal{ImGuiCol_Button, palette.normal};
    const ScopedStyleColor hovered{ImGuiCol_ButtonHovered, palette.hovered};
    const ScopedStyleColor active{ImGuiCol_ButtonActive, palette.active};
    const ScopedStyleColor textColor{ImGuiCol_Text, palette.text};
    const ScopedStyleColor border{ImGuiCol_Border, palette.border};
    const bool pressed{ImGui::Button("##action", size)};

    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 maximum{ImGui::GetItemRectMax()};
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    const ImU32 color{ImGui::GetColorU32(palette.text)};
    if (options.busy) {
        const ImVec2 busySize{ImGui::CalcTextSize("...")};
        draw.AddText({minimum.x + (size.x - busySize.x) * 0.5F, minimum.y + (size.y - busySize.y) * 0.5F}, color, "...");
        return false;
    }

    float left{options.contentAlignment == ButtonContentAlignment::Leading ? minimum.x + options.contentInset
                                                                           : minimum.x + (size.x - contentWidth) * 0.5F};
    if (options.icon.has_value()) {
        DrawVectorIcon(*options.icon, {left, minimum.y + (maximum.y - minimum.y - iconSize) * 0.5F}, iconSize, color);
        left += iconOnly ? 0.0F : iconSize + metrics.spacingSm;
    }
    if (!iconOnly) {
        const std::string visible{label};
        draw.AddText({left, minimum.y + (maximum.y - minimum.y - textSize.y) * 0.5F}, color, visible.c_str());
    }
    return pressed;
}

bool IconAction(const WidgetId id, const VectorIcon icon, const std::string_view tooltip, const ButtonOptions& options) {
    ButtonOptions resolved{options};
    resolved.icon = icon;
    if (resolved.size == WidgetSize::Default) {
        resolved.size = WidgetSize::Icon;
    }
    const bool pressed{ActionButton(id, {}, resolved)};
    if (!tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        const std::string visible{tooltip};
        ImGui::SetTooltip("%s", visible.c_str());
    }
    return pressed;
}

} // namespace px::ui
