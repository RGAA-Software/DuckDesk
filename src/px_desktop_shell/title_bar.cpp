#include "title_bar.h"

#include "brand_logo.h"
#include "window_host.h"

#include "px_ui/layout_metrics.h"
#include "px_ui/theme_tokens.h"
#include "px_ui/vector_icon.h"

#include <imgui.h>

#include <string>
#include <string_view>

#include "version_config.h"

namespace px::desktop {
namespace {

bool CircularCaptionButton(const px::ui::VectorIcon icon, const std::string_view id, const std::string_view tooltip, const bool destructive) {
    const float size{px::ui::Scale(static_cast<float>(kCaptionButtonLogicalWidth))};
    const float iconSize{px::ui::Scale(16.0F)};
    const float radius{px::ui::Scale(14.0F)};
    const std::string controlId{"##title-" + std::string{id}};
    const bool pressed{ImGui::InvisibleButton(controlId.c_str(), {size, size})};
    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 center{minimum.x + size * 0.5F, minimum.y + size * 0.5F};
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        const ImVec4 background{destructive ? tokens.destructive : (ImGui::IsItemActive() ? tokens.accent : tokens.muted)};
        constexpr int circleSegments{48};
        ImGui::GetWindowDrawList()->AddCircleFilled(center, radius, ImGui::GetColorU32(background), circleSegments);
    }
    const ImVec4 iconColor{destructive && ImGui::IsItemHovered() ? tokens.destructiveForeground : tokens.foreground};
    px::ui::DrawVectorIcon(icon, {center.x - iconSize * 0.5F, center.y - iconSize * 0.5F}, iconSize, ImGui::GetColorU32(iconColor));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        const std::string visible{tooltip};
        ImGui::SetTooltip("%s", visible.c_str());
    }
    return pressed;
}

} // namespace

bool DrawTitleBar(WindowHost& window, const WindowChromeConfig& chrome, const BrandLogo& logo, const std::string_view titleOverride) {
    const float titleBarHeight{px::ui::Scale(static_cast<float>(kTitleBarLogicalHeight))};
    const float buttonWidth{px::ui::Scale(static_cast<float>(kCaptionButtonLogicalWidth))};
    const float buttonCount{1.0F + (chrome.showMinimizeButton ? 1.0F : 0.0F) + (chrome.showMaximizeButton ? 1.0F : 0.0F)};
    constexpr ImGuiWindowFlags titleFlags{ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{});
    ImGui::BeginChild("PixelsTitleBar", {0.0F, titleBarHeight}, ImGuiChildFlags_None, titleFlags);
    ImGui::PopStyleVar(2);

    const ImVec2 origin{ImGui::GetCursorScreenPos()};
    const float logoSize{px::ui::Scale(20.0F)};
    const float logoLeft{origin.x + px::ui::Scale(15.0F)};
    const float logoTop{origin.y + (titleBarHeight - logoSize) * 0.5F};
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    logo.Draw({logoLeft, logoTop}, logoSize);
    const std::string title{titleOverride.empty() ? "Pixels(V" PROJECT_VERSION ")" : titleOverride};
    const ImVec2 titleSize{ImGui::CalcTextSize(title.c_str())};
    ImGui::GetWindowDrawList()->AddText({logoLeft + logoSize + px::ui::Scale(8.0F), origin.y + (titleBarHeight - titleSize.y) * 0.5F},
                                        ImGui::GetColorU32(tokens.foreground), title.c_str());

    ImGui::SetCursorScreenPos({origin.x + ImGui::GetContentRegionAvail().x - buttonWidth * buttonCount, origin.y});
    if (chrome.showMinimizeButton) {
        if (CircularCaptionButton(px::ui::VectorIcon::Minimize, "window-minimize", "Minimize", false)) {
            window.Minimize();
        }
        ImGui::SameLine(0.0F, 0.0F);
    }
    if (chrome.showMaximizeButton) {
        if (CircularCaptionButton(window.IsMaximized() ? px::ui::VectorIcon::Restore : px::ui::VectorIcon::Maximize, "window-maximize",
                                  window.IsMaximized() ? "Restore" : "Maximize", false)) {
            window.ToggleMaximize();
        }
        ImGui::SameLine(0.0F, 0.0F);
    }
    const bool keepRunning{!CircularCaptionButton(px::ui::VectorIcon::Close, "window-close", "Close", true)};
    ImGui::EndChild();
    return keepRunning;
}

} // namespace px::desktop
