#include "px_ui/components/navigation.h"

#include "px_ui/components/button.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <string>

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
    ButtonOptions options{.variant = ButtonVariant::Ghost, .size = WidgetSize::Sm, .width = width};
    const ScopedStyleColor text{ImGuiCol_Text, selected ? tokens.foreground : tokens.mutedForeground};
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

} // namespace px::ui
