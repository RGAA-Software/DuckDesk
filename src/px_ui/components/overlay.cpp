#include "px_ui/components/overlay.h"

#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <string>

namespace px::ui {

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
    ImGui::SetNextWindowSizeConstraints({width * metrics.scale, 0.0F},
                                        {width * metrics.scale, viewport.WorkSize.y - metrics.spacingXl * 2.0F});
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
    open_ = ImGui::BeginPopupModal(id_.c_str(), {}, flags);
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

void Tooltip(const std::string_view text) {
    if (!text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        const std::string visible{text};
        ImGui::SetTooltip("%s", visible.c_str());
    }
}

bool MenuAction(const WidgetId id, const std::string_view label, const bool enabled, const bool selected) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    ImGui::PushID(id.value.data(), id.value.data() + id.value.size());
    ImGui::PushStyleColor(ImGuiCol_Header, tokens.accent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tokens.muted);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, tokens.accent);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{metrics.spacingSm, metrics.spacingSm});
    const std::string visible{label};
    const bool pressed{ImGui::MenuItem(visible.c_str(), {}, selected, enabled)};
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return pressed;
}

} // namespace px::ui
