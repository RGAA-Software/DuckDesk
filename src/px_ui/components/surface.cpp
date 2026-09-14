#include "px_ui/components/surface.h"

#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <algorithm>

namespace px::ui {

CardScope::CardScope(const WidgetId id, const ImVec2 size, const ImGuiWindowFlags flags, const ImGuiChildFlags childFlags,
                     const std::optional<ImVec2> padding)
    : id_{id.value} {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tokens.card);
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, metrics.cardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, metrics.borderWidth);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding.value_or(ImVec2{metrics.spacingLg, metrics.spacingLg}));
    visible_ = ImGui::BeginChild(id_.c_str(), size, childFlags, flags);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

CardScope::~CardScope() {
    ImGui::EndChild();
}

bool CardScope::Visible() const noexcept {
    return visible_;
}

void PageTitle(const std::string_view title) {
    const float scale{ImGui::GetStyle().FontScaleDpi};
    ImGui::SetWindowFontScale(1.25F);
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    ImGui::SetWindowFontScale(1.0F);
    static_cast<void>(scale);
}

void SectionTitle(const std::string_view title) {
    ImGui::SetWindowFontScale(1.08F);
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    ImGui::SetWindowFontScale(1.0F);
}

void StrongText(const std::string_view text) {
    const bool hasMediumFont{ImGui::GetIO().Fonts->Fonts.Size > 1};
    if (hasMediumFont) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1], 0.0F); // NOLINT(gammaray-raw-pointer-boundary): Dear ImGui font registry ABI.
    }
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    if (hasMediumFont) {
        ImGui::PopFont();
    }
}

void MutedText(const std::string_view text) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    ImGui::PushStyleColor(ImGuiCol_Text, tokens.mutedForeground);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
}

void StatusBadge(const std::string_view text, const BadgeVariant variant) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    ImVec4 background{tokens.secondary};
    ImVec4 foreground{tokens.secondaryForeground};
    ImVec4 border{tokens.border};
    switch (variant) {
    case BadgeVariant::Success:
        background = {tokens.success.x, tokens.success.y, tokens.success.z, 0.14F};
        foreground = tokens.success;
        border = {tokens.success.x, tokens.success.y, tokens.success.z, 0.38F};
        break;
    case BadgeVariant::Warning:
        background = {tokens.warning.x, tokens.warning.y, tokens.warning.z, 0.14F};
        foreground = tokens.warning;
        border = {tokens.warning.x, tokens.warning.y, tokens.warning.z, 0.38F};
        break;
    case BadgeVariant::Destructive:
        background = {tokens.destructive.x, tokens.destructive.y, tokens.destructive.z, 0.14F};
        foreground = tokens.destructive;
        border = {tokens.destructive.x, tokens.destructive.y, tokens.destructive.z, 0.38F};
        break;
    case BadgeVariant::Outline:
        background = {tokens.background.x, tokens.background.y, tokens.background.z, 0.0F};
        foreground = tokens.foreground;
        break;
    case BadgeVariant::Default:
        background = tokens.primary;
        foreground = tokens.primaryForeground;
        border = tokens.primary;
        break;
    case BadgeVariant::Secondary:
    default:
        break;
    }
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec2 textSize{ImGui::CalcTextSize(text.data(), text.data() + text.size())};
    const ImVec2 size{textSize.x + metrics.spacingMd * 2.0F, textSize.y + metrics.spacingXs * 2.0F};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    ImGui::Dummy(size);
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    draw.AddRectFilled(minimum, {minimum.x + size.x, minimum.y + size.y}, ImGui::GetColorU32(background), size.y * 0.5F);
    draw.AddRect(minimum, {minimum.x + size.x, minimum.y + size.y}, ImGui::GetColorU32(border), size.y * 0.5F);
    const std::string visible{text};
    draw.AddText({minimum.x + metrics.spacingMd, minimum.y + metrics.spacingXs}, ImGui::GetColorU32(foreground), visible.c_str());
}

void HorizontalSeparator() {
    const ThemeTokens tokens{CurrentThemeTokens()};
    ImGui::PushStyleColor(ImGuiCol_Separator, tokens.border);
    ImGui::Separator();
    ImGui::PopStyleColor();
}

} // namespace px::ui
