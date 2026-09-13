#include "px_ui/components/identity.h"

#include "px_ui/components/overlay.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

namespace px::ui {

bool AvatarButton(const WidgetId id, const std::string_view tooltip, const float size, const bool emphasized) {
    const ScopedId scopedId{id.value};
    const ThemeTokens tokens{CurrentThemeTokens()};
    const bool pressed{ImGui::InvisibleButton("##avatar", {size, size})};
    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 maximum{ImGui::GetItemRectMax()};
    const ImVec2 center{minimum.x + size * 0.5F, minimum.y + size * 0.5F};
    const ImVec4 normal{emphasized ? tokens.primary : tokens.secondary};
    const ImVec4 hovered{emphasized ? tokens.ring : tokens.accent};
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    draw.AddCircleFilled(center, size * 0.5F, ImGui::GetColorU32(ImGui::IsItemHovered() ? hovered : normal), 48);
    const float iconSize{size * 0.52F};
    DrawVectorIcon(VectorIcon::User, {center.x - iconSize * 0.5F, center.y - iconSize * 0.5F}, iconSize,
                   ImGui::GetColorU32(emphasized ? tokens.primaryForeground : tokens.secondaryForeground));
    Tooltip(tooltip);
    return pressed;
}

} // namespace px::ui
