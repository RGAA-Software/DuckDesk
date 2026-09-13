#include "px_ui/components/data_view.h"

#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace px::ui {

void KeyValueRow(const std::string_view label, const std::string_view value, const float labelWidth) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    ImGui::PushStyleColor(ImGuiCol_Text, tokens.mutedForeground);
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::PopStyleColor();
    ImGui::SameLine(labelWidth);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}

void EmptyState(const VectorIcon icon, const std::string_view title, const std::string_view description) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const float width{ImGui::GetContentRegionAvail().x};
    const ImVec2 start{ImGui::GetCursorScreenPos()};
    const float iconSize{metrics.iconLg * 1.4F};
    DrawVectorIcon(icon, {start.x + (width - iconSize) * 0.5F, start.y}, iconSize, ImGui::GetColorU32(tokens.mutedForeground));
    ImGui::Dummy({width, iconSize + metrics.spacingSm});
    const ImVec2 titleSize{ImGui::CalcTextSize(title.data(), title.data() + title.size())};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - titleSize.x) * 0.5F);
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    const ImVec2 descriptionSize{ImGui::CalcTextSize(description.data(), description.data() + description.size())};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - descriptionSize.x) * 0.5F);
    ImGui::PushStyleColor(ImGuiCol_Text, tokens.mutedForeground);
    ImGui::TextUnformatted(description.data(), description.data() + description.size());
    ImGui::PopStyleColor();
}

void LoadingSpinner(const WidgetId id, const float radius) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec2 size{radius * 2.0F + metrics.spacingXs * 2.0F, radius * 2.0F + metrics.spacingXs * 2.0F};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    const std::string label{"##spinner-" + std::string{id.value}};
    ImGui::InvisibleButton(label.c_str(), size);
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    const ImVec2 center{minimum.x + size.x * 0.5F, minimum.y + size.y * 0.5F};
    constexpr int segments{24};
    const float start{static_cast<float>(std::fmod(ImGui::GetTime() * 4.0, 6.283185307179586))};
    draw.PathClear();
    for (int index{}; index < segments; ++index) {
        const float angle{start + static_cast<float>(index) / static_cast<float>(segments - 1) * 4.7F};
        draw.PathLineTo({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius});
    }
    draw.PathStroke(ImGui::GetColorU32(tokens.primary), ImDrawFlags_None, std::max(2.0F, metrics.borderWidth * 2.0F));
}

void Progress(const float fraction, const float width) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const float resolvedWidth{width > 0.0F ? width : ImGui::GetContentRegionAvail().x};
    const ImVec2 size{resolvedWidth, metrics.spacingSm};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    ImGui::Dummy(size);
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    draw.AddRectFilled(minimum, {minimum.x + size.x, minimum.y + size.y}, ImGui::GetColorU32(tokens.secondary), size.y * 0.5F);
    const float clamped{std::clamp(fraction, 0.0F, 1.0F)};
    draw.AddRectFilled(minimum, {minimum.x + size.x * clamped, minimum.y + size.y}, ImGui::GetColorU32(tokens.primary), size.y * 0.5F);
}

bool SelectableRow(const WidgetId id, const std::string_view label, const bool selected, const ImGuiSelectableFlags flags, const ImVec2 size) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ScopedId scopedId{id.value};
    ImGui::PushStyleColor(ImGuiCol_Header, tokens.accent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tokens.muted);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, tokens.accent);
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2{0.0F, 0.5F});
    const std::string visible{label};
    const bool pressed{
        ImGui::Selectable(visible.c_str(), selected, flags, size.x == 0.0F && size.y == 0.0F ? ImVec2{0.0F, metrics.controlDefault} : size)};
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return pressed;
}

bool SelectableIconRow(const WidgetId id, const VectorIcon icon, const std::string_view label, const bool selected, const ImGuiSelectableFlags flags,
                       const ImVec2 size) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ScopedId scopedId{id.value};
    ImGui::PushStyleColor(ImGuiCol_Header, tokens.accent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tokens.muted);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, tokens.accent);
    const ImVec2 resolvedSize{size.x == 0.0F && size.y == 0.0F ? ImVec2{0.0F, metrics.controlDefault} : size};
    const bool pressed{ImGui::Selectable("##icon-row", selected, flags, resolvedSize)};
    ImGui::PopStyleColor(3);
    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 maximum{ImGui::GetItemRectMax()};
    const float iconSize{metrics.iconDefault};
    DrawVectorIcon(icon, {minimum.x + metrics.spacingSm, minimum.y + (maximum.y - minimum.y - iconSize) * 0.5F}, iconSize,
                   ImGui::GetColorU32(icon == VectorIcon::Folder ? tokens.primary : tokens.mutedForeground));
    const std::string visible{label};
    const ImVec2 textSize{ImGui::CalcTextSize(visible.c_str())};
    ImGui::GetWindowDrawList()->AddText({minimum.x + metrics.spacingSm * 2.0F + iconSize, minimum.y + (maximum.y - minimum.y - textSize.y) * 0.5F},
                                        ImGui::GetColorU32(tokens.foreground), visible.c_str());
    return pressed;
}

} // namespace px::ui
