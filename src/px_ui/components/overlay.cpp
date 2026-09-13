#include "px_ui/components/overlay.h"

#include "px_ui/components/button.h"
#include "px_ui/components/surface.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace px::ui {
namespace {

ImVec4 ToneColor(const BadgeVariant tone, const ThemeTokens& tokens) noexcept {
    switch (tone) {
    case BadgeVariant::Success:
        return tokens.success;
    case BadgeVariant::Warning:
        return tokens.warning;
    case BadgeVariant::Destructive:
        return tokens.destructive;
    case BadgeVariant::Default:
        return tokens.primary;
    case BadgeVariant::Outline:
    case BadgeVariant::Secondary:
    default:
        return tokens.mutedForeground;
    }
}

} // namespace

void OpenModal(const WidgetId id) {
    const std::string value{id.value};
    ImGui::OpenPopup(value.c_str());
}

void OpenPopup(const WidgetId id) {
    const std::string value{id.value};
    ImGui::OpenPopup(value.c_str());
}

PopupScope::PopupScope(const WidgetId id) : id_{id.value} {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    ImVec4 background{tokens.popover};
    background.w = EnhancedVisualEffectsEnabled() ? 0.94F : 1.0F;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, metrics.popupRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{metrics.spacingSm, metrics.spacingSm});
    open_ = ImGui::BeginPopup(id_.c_str());
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

PopupScope::~PopupScope() {
    if (open_) {
        ImGui::EndPopup();
    }
}

bool PopupScope::Open() const noexcept {
    return open_;
}

ModalScope::ModalScope(const WidgetId id, const float width, const ImGuiWindowFlags flags) : id_{id.value} {
    const ImGuiViewport& viewport{*ImGui::GetMainViewport()};
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    ImGui::SetNextWindowPos(viewport.GetCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
    ImGui::SetNextWindowSizeConstraints({width * metrics.scale, 0.0F}, {width * metrics.scale, viewport.WorkSize.y - metrics.spacingXl * 2.0F});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, tokens.popover);
    if (EnhancedVisualEffectsEnabled()) {
        ImVec4 translucent{tokens.popover};
        translucent.w = 0.94F;
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_PopupBg, translucent);
    }
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, metrics.popupRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{metrics.spacingXl, metrics.spacingXl});
    constexpr ImGuiWindowFlags dialogFlags{ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoSavedSettings};
    open_ = ImGui::BeginPopupModal(id_.c_str(), {}, flags | dialogFlags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

ModalScope::~ModalScope() {
    if (open_) {
        ImGui::EndPopup();
    }
}

bool ModalScope::Open() const noexcept {
    return open_;
}

ContextMenuScope::ContextMenuScope(const WidgetId id) : id_{id.value} {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    ImVec4 background{tokens.popover};
    background.w = EnhancedVisualEffectsEnabled() ? 0.94F : 1.0F;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, metrics.popupRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{metrics.spacingSm, metrics.spacingSm});
    ImGui::SetNextWindowSizeConstraints({220.0F * metrics.scale, 0.0F}, {420.0F * metrics.scale, 520.0F * metrics.scale});
    open_ = ImGui::BeginPopupContextItem(id_.c_str());
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

ContextMenuScope::~ContextMenuScope() {
    if (open_) {
        ImGui::EndPopup();
    }
}

bool ContextMenuScope::Open() const noexcept {
    return open_;
}

PopupMenuScope::PopupMenuScope(const WidgetId id) : id_{id.value} {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    ImVec4 background{tokens.popover};
    background.w = EnhancedVisualEffectsEnabled() ? 0.94F : 1.0F;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, metrics.popupRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{metrics.spacingSm, metrics.spacingSm});
    ImGui::SetNextWindowSizeConstraints({220.0F * metrics.scale, 0.0F}, {420.0F * metrics.scale, 520.0F * metrics.scale});
    open_ = ImGui::BeginPopup(id_.c_str());
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

PopupMenuScope::~PopupMenuScope() {
    if (open_)
        ImGui::EndPopup();
}

bool PopupMenuScope::Open() const noexcept {
    return open_;
}

bool DialogHeader(const WidgetId closeId, const std::string_view title, const std::string_view description, const DialogHeaderOptions& options) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec2 start{ImGui::GetCursorScreenPos()};
    const float availableWidth{ImGui::GetContentRegionAvail().x};
    const float mediaSize{options.icon.has_value() ? metrics.controlLg : 0.0F};
    const float contentLeft{start.x + (options.icon.has_value() ? mediaSize + metrics.spacingMd : 0.0F)};
    const float closeSize{options.closeable ? metrics.controlXs : 0.0F};
    const float titleHeight{ImGui::GetTextLineHeight() * 1.125F};
    const float headerHeight{std::max({mediaSize, closeSize, titleHeight})};
    bool closeRequested{};
    if (options.closeable) {
        ImGui::SetCursorScreenPos({start.x + availableWidth - closeSize, start.y + (headerHeight - closeSize) * 0.5F});
        closeRequested = IconAction(closeId, VectorIcon::Close, {}, {.variant = ButtonVariant::Ghost, .size = WidgetSize::IconXs, .circular = true});
    }
    if (options.icon.has_value()) {
        ImDrawList& draw{*ImGui::GetWindowDrawList()};
        const float mediaTop{start.y + (headerHeight - mediaSize) * 0.5F};
        draw.AddRectFilled({start.x, mediaTop}, {start.x + mediaSize, mediaTop + mediaSize}, ImGui::GetColorU32(tokens.muted), metrics.controlRadius);
        const float iconSize{metrics.iconLg};
        const ImVec4 tone{ToneColor(options.tone, tokens)};
        DrawVectorIcon(*options.icon, {start.x + (mediaSize - iconSize) * 0.5F, mediaTop + (mediaSize - iconSize) * 0.5F}, iconSize,
                       ImGui::GetColorU32(tone));
    }
    ImGui::SetWindowFontScale(1.125F);
    ImGui::SetCursorScreenPos({contentLeft, start.y + (headerHeight - titleHeight) * 0.5F});
    StrongText(title);
    ImGui::SetWindowFontScale(1.0F);
    float contentBottom{start.y + headerHeight};
    if (!description.empty()) {
        ImGui::SetCursorScreenPos({start.x, contentBottom + metrics.spacingMd});
        ImGui::PushStyleColor(ImGuiCol_Text, tokens.mutedForeground);
        ImGui::PushTextWrapPos(start.x + availableWidth);
        ImGui::TextWrapped("%.*s", static_cast<int>(description.size()), description.data());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        contentBottom = ImGui::GetItemRectMax().y;
    }
    ImGui::SetCursorScreenPos({start.x, contentBottom + metrics.spacingMd});
    if (closeRequested) {
        ImGui::CloseCurrentPopup();
    }
    return closeRequested;
}

void DialogFooter(const float actionsWidth) {
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    ImGui::Dummy({0.0F, metrics.spacingMd});
    if (actionsWidth > 0.0F) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - actionsWidth));
    }
}

void ShowTooltip(const std::string_view text) {
    if (text.empty()) {
        return;
    }
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const std::string visible{text};
    constexpr float tooltipTextWidth{320.0F};
    const float maximumTextWidth{tooltipTextWidth * metrics.scale};
    const float naturalTextWidth{ImGui::CalcTextSize(visible.c_str()).x};
    const float resolvedTextWidth{std::min(naturalTextWidth, maximumTextWidth)};
    ImGui::PushStyleColor(ImGuiCol_PopupBg, tokens.popover);
    ImGui::PushStyleColor(ImGuiCol_Text, tokens.foreground);
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, metrics.controlRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{metrics.spacingMd, metrics.spacingSm});
    ImGui::SetNextWindowSize({resolvedTextWidth + metrics.spacingMd * 2.0F, 0.0F}, ImGuiCond_Always);
    if (ImGui::BeginTooltip()) {
        ImGui::SetWindowFontScale(0.875F);
        if (naturalTextWidth > maximumTextWidth) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maximumTextWidth);
            ImGui::TextWrapped("%s", visible.c_str());
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextUnformatted(visible.c_str());
        }
        ImGui::SetWindowFontScale(1.0F);
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

void Tooltip(const std::string_view text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ShowTooltip(text);
    }
}

void MenuSeparator() {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const ScopedStyleColor separator{ImGuiCol_Separator, tokens.border};
    ImGui::Separator();
}

bool MenuAction(const WidgetId id, const std::string_view label, const MenuActionOptions& options) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabled{!options.enabled};
    const float height{metrics.controlSm};
    const ImVec2 textSize{ImGui::CalcTextSize(label.data(), label.data() + label.size())};
    const ImVec2 shortcutSize{ImGui::CalcTextSize(options.shortcut.data(), options.shortcut.data() + options.shortcut.size())};
    const float naturalWidth{metrics.spacingSm * 2.0F + textSize.x + (options.icon.has_value() ? metrics.iconDefault + metrics.spacingSm : 0.0F) +
                             (!options.shortcut.empty() ? shortcutSize.x + metrics.spacingXl : 0.0F) +
                             (options.selected ? metrics.iconDefault + metrics.spacingSm : 0.0F)};
    const float itemWidth{std::max(204.0F * metrics.scale, naturalWidth)};
    const bool pressed{ImGui::InvisibleButton("##menu-action", {itemWidth, height})};
    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 maximum{ImGui::GetItemRectMax()};
    const float itemHeight{maximum.y - minimum.y};
    const ImVec4 foreground{!options.enabled                                  ? tokens.mutedForeground
                            : options.variant == MenuItemVariant::Destructive ? tokens.destructive
                                                                              : tokens.foreground};
    const float iconSize{metrics.iconDefault};
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    if (options.enabled && (options.selected || ImGui::IsItemHovered())) {
        draw.AddRectFilled(minimum, maximum, ImGui::GetColorU32(tokens.accent), metrics.controlRadius);
    }
    float textLeft{minimum.x + metrics.spacingSm};
    if (options.icon.has_value()) {
        DrawVectorIcon(*options.icon, {textLeft, minimum.y + (itemHeight - iconSize) * 0.5F}, iconSize, ImGui::GetColorU32(foreground));
        textLeft += iconSize + metrics.spacingSm;
    }
    const std::string visible{label};
    draw.AddText({textLeft, minimum.y + (itemHeight - textSize.y) * 0.5F}, ImGui::GetColorU32(foreground), visible.c_str());
    if (!options.shortcut.empty()) {
        const std::string shortcut{options.shortcut};
        const float rightInset{options.selected ? iconSize + metrics.spacingSm * 2.0F : metrics.spacingSm};
        draw.AddText({maximum.x - rightInset - shortcutSize.x, minimum.y + (height - shortcutSize.y) * 0.5F},
                     ImGui::GetColorU32(tokens.mutedForeground), shortcut.c_str());
    }
    if (options.selected) {
        DrawVectorIcon(VectorIcon::Check, {maximum.x - metrics.spacingSm - iconSize, minimum.y + (itemHeight - iconSize) * 0.5F}, iconSize,
                       ImGui::GetColorU32(tokens.primary));
    }
    if (pressed) {
        ImGui::CloseCurrentPopup();
    }
    return pressed;
}

bool MenuAction(const WidgetId id, const std::string_view label, const bool enabled, const bool selected) {
    return MenuAction(id, label, {.enabled = enabled, .selected = selected});
}

} // namespace px::ui
