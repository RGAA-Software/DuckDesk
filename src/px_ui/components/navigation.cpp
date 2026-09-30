#include "px_ui/components/navigation.h"

#include <imgui.h>

#include <string>

#include "px_ui/components/button.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

namespace px::ui {

bool NavigationItem(const WidgetId id, const VectorIcon icon, const std::string_view label, const bool selected, const float width,
                    const WidgetSize size, const float height, const float leadingIconInset, const bool showSelectionIndicator) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    ButtonOptions options{.variant = selected ? ButtonVariant::Accent : ButtonVariant::Ghost,
                          .size = size,
                          .icon = icon,
                          .width = width,
                          .height = height,
                          .contentAlignment = leadingIconInset > 0.0F ? ButtonContentAlignment::Leading : ButtonContentAlignment::Center,
                          .contentInset = leadingIconInset};
    const bool pressed{ActionButton(id, label, options)};
    if (selected && showSelectionIndicator) {
        ImDrawList& draw{*ImGui::GetWindowDrawList()};
        draw.AddRectFilled({minimum.x, minimum.y + metrics.spacingXs},
                           {minimum.x + std::max(metrics.borderWidth * 2.0F, 2.0F), minimum.y + ImGui::GetItemRectSize().y - metrics.spacingXs},
                           ImGui::GetColorU32(tokens.primary), metrics.borderWidth * 1.5F);
    }
    return pressed;
}

bool TabItem(const WidgetId id, const std::string_view label, const bool selected, const float width) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    ButtonOptions options{.variant = ButtonVariant::Ghost,
                          .size = WidgetSize::Default,
                          .width = width,
                          .textColor = selected ? tokens.primaryText : tokens.mutedForeground};
    const bool pressed{ActionButton(id, label, options)};
    if (selected) {
        ImDrawList& draw{*ImGui::GetWindowDrawList()};
        const ImVec2 maximum{ImGui::GetItemRectMax()};
        draw.AddRectFilled({minimum.x + metrics.spacingSm, maximum.y - metrics.borderWidth * 2.0F}, {maximum.x - metrics.spacingSm, maximum.y},
                           ImGui::GetColorU32(tokens.primary), metrics.borderWidth);
    }
    return pressed;
}

bool SegmentedItem(const WidgetId id, const std::string_view label, const bool selected, const float width, const WidgetSize size) {
    return ActionButton(id, label,
                        ButtonOptions{.variant = selected ? ButtonVariant::Primary : ButtonVariant::Secondary, .size = size, .width = width});
}

bool SegmentedControl(const WidgetId id, int& value, const std::span<const SelectOption> options) {
    const ScopedId scopedId{id.value};
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const float inset{metrics.spacingXs};
    const float optionHeight{metrics.controlDefault - inset * 2.0F};
    float totalWidth{inset * 2.0F};
    for (const auto& option : options)
        totalWidth += ImGui::CalcTextSize(option.label.data(), option.label.data() + option.label.size()).x + metrics.spacingMd * 2.0F;
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    draw.AddRectFilled(minimum, {minimum.x + totalWidth, minimum.y + metrics.controlDefault}, ImGui::GetColorU32(tokens.secondary),
                       metrics.controlRadius);
    ImGui::BeginGroup();
    float optionLeft{minimum.x + inset};
    bool changed{};
    for (const auto& option : options) {
        const ImVec2 textSize{ImGui::CalcTextSize(option.label.data(), option.label.data() + option.label.size())};
        const float optionWidth{textSize.x + metrics.spacingMd * 2.0F};
        ImGui::SetCursorScreenPos({optionLeft, minimum.y + inset});
        ImGui::PushID(option.value);
        if (ImGui::InvisibleButton("segment", {optionWidth, optionHeight}, ImGuiButtonFlags_EnableNav) && value != option.value) {
            value = option.value;
            changed = true;
        }
        const ImVec2 optionMinimum{ImGui::GetItemRectMin()};
        const ImVec2 optionMaximum{ImGui::GetItemRectMax()};
        if (value == option.value || ImGui::IsItemHovered()) {
            draw.AddRectFilled(optionMinimum, optionMaximum, ImGui::GetColorU32(value == option.value ? tokens.card : tokens.muted),
                               metrics.controlRadius - inset * 0.5F);
        }
        if (ImGui::IsItemFocused()) draw.AddRect(optionMinimum, optionMaximum, ImGui::GetColorU32(tokens.ring), metrics.controlRadius);
        const std::string label{option.label};
        draw.AddText({optionLeft + metrics.spacingMd, minimum.y + (metrics.controlDefault - textSize.y) * 0.5F},
                     ImGui::GetColorU32(value == option.value ? tokens.foreground : tokens.mutedForeground), label.c_str());
        ImGui::PopID();
        optionLeft += optionWidth;
    }
    ImGui::SetCursorScreenPos({minimum.x, minimum.y + metrics.controlDefault});
    ImGui::Dummy({totalWidth, 0.0F});
    ImGui::EndGroup();
    return changed;
}

}  // namespace px::ui
