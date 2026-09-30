#include "px_ui/components/data_view.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "px_ui/components/overlay.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

namespace px::ui {

std::string EllipsizedText(const std::string_view text, const float maximumWidth) {
    if (ImGui::CalcTextSize(text.data(), text.data() + text.size()).x <= maximumWidth) return std::string{text};
    constexpr std::string_view suffix{"..."};
    if (ImGui::CalcTextSize(suffix.data(), suffix.data() + suffix.size()).x > maximumWidth) return {};
    std::size_t fittingLength{};
    for (std::size_t boundary{}; boundary < text.size();) {
        ++boundary;
        while (boundary < text.size() && (static_cast<unsigned char>(text[boundary]) & 0xC0U) == 0x80U) ++boundary;
        const std::string candidate{std::string{text.substr(0, boundary)} + std::string{suffix}};
        if (ImGui::CalcTextSize(candidate.c_str()).x > maximumWidth) break;
        fittingLength = boundary;
    }
    return std::string{text.substr(0, fittingLength)} + std::string{suffix};
}

void ClippedText(const std::string_view text, const float width, const float height) {
    const float resolvedWidth{std::max(1.0F, width > 0.0F ? width : ImGui::GetContentRegionAvail().x)};
    const float textHeight{ImGui::GetTextLineHeight()};
    const float resolvedHeight{std::max(textHeight, height)};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    const ImVec2 maximum{minimum.x + resolvedWidth, minimum.y + resolvedHeight};
    const std::string visible{EllipsizedText(text, resolvedWidth)};
    ImGui::Dummy({resolvedWidth, maximum.y - minimum.y});
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    draw.PushClipRect(minimum, maximum, true);
    draw.AddText({minimum.x, minimum.y + (resolvedHeight - textHeight) * 0.5F}, ImGui::GetColorU32(ImGuiCol_Text), visible.c_str());
    draw.PopClipRect();
    if (visible != text) Tooltip(text);
}

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
    const ImVec2 titleSize{ImGui::CalcTextSize(title.data(), title.data() + title.size(), false, width)};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - titleSize.x) * 0.5F);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + titleSize.x);
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    ImGui::PopTextWrapPos();
    const ImVec2 descriptionSize{ImGui::CalcTextSize(description.data(), description.data() + description.size(), false, width)};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - descriptionSize.x) * 0.5F);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + descriptionSize.x);
    ImGui::PushStyleColor(ImGuiCol_Text, tokens.mutedForeground);
    ImGui::TextUnformatted(description.data(), description.data() + description.size());
    ImGui::PopTextWrapPos();
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
    const float availableWidth{ImGui::GetContentRegionAvail().x};
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
    const std::string visible{EllipsizedText(label, std::max(1.0F, availableWidth - metrics.spacingSm * 3.0F - iconSize))};
    const ImVec2 textSize{ImGui::CalcTextSize(visible.c_str())};
    ImGui::GetWindowDrawList()->PushClipRect(minimum, {minimum.x + availableWidth, maximum.y}, true);
    ImGui::GetWindowDrawList()->AddText({minimum.x + metrics.spacingSm * 2.0F + iconSize, minimum.y + (maximum.y - minimum.y - textSize.y) * 0.5F},
                                        ImGui::GetColorU32(tokens.foreground), visible.c_str());
    ImGui::GetWindowDrawList()->PopClipRect();
    if (visible != label) Tooltip(label);
    return pressed;
}

}  // namespace px::ui
