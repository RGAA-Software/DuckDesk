#include "title_bar.h"

#include "window_host.h"

#include "px_ui/layout_metrics.h"
#include "px_ui/vector_icon.h"

#include <imgui.h>

namespace px::desktop {

bool DrawTitleBar(WindowHost& window, const WindowChromeConfig& chrome) {
    constexpr float captionButtonWidth{46.0F};
    const float buttonWidth{px::ui::Scale(captionButtonWidth)};
    const float buttonCount{1.0F + (chrome.showMinimizeButton ? 1.0F : 0.0F) + (chrome.showMaximizeButton ? 1.0F : 0.0F)};
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, px::ui::Scale(ImVec2{4.0F, 0.0F}));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Pixels");

    ImGui::SameLine(ImGui::GetWindowWidth() - buttonWidth * buttonCount - px::ui::Scale(8.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px::ui::Scale(5.0F));
    if (chrome.showMinimizeButton) {
        if (px::ui::IconOnlyButton(px::ui::VectorIcon::Minimize, "window-minimize", "Minimize", {buttonWidth, px::ui::Scale(32.0F)})) {
            window.Minimize();
        }
        ImGui::SameLine();
    }
    if (chrome.showMaximizeButton) {
        if (px::ui::IconOnlyButton(window.IsMaximized() ? px::ui::VectorIcon::Restore : px::ui::VectorIcon::Maximize, "window-maximize",
                                   window.IsMaximized() ? "Restore" : "Maximize", {buttonWidth, px::ui::Scale(32.0F)})) {
            window.ToggleMaximize();
        }
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.90F, 0.16F, 0.22F, 1.0F});
    const bool keepRunning{!px::ui::IconOnlyButton(px::ui::VectorIcon::Close, "window-close", "Close", {buttonWidth, px::ui::Scale(32.0F)})};
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::Separator();
    return keepRunning;
}

} // namespace px::desktop
