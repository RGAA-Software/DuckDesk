#include "title_bar.h"

#include "window_host.h"

#include "px_ui/layout_metrics.h"

#include <imgui.h>

namespace px::desktop {

bool DrawTitleBar(WindowHost& window) {
    constexpr float captionButtonWidth{46.0F};
    constexpr float captionButtonCount{3.0F};
    const float buttonWidth{px::ui::Scale(captionButtonWidth)};
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, px::ui::Scale(ImVec2{4.0F, 0.0F}));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Pixels");

    ImGui::SameLine(ImGui::GetWindowWidth() - buttonWidth * captionButtonCount - px::ui::Scale(8.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px::ui::Scale(5.0F));
    if (ImGui::Button("_", {buttonWidth, px::ui::Scale(32.0F)})) {
        window.Minimize();
    }
    ImGui::SameLine();
    if (ImGui::Button(window.IsMaximized() ? "[]" : "[ ]", {buttonWidth, px::ui::Scale(32.0F)})) {
        window.ToggleMaximize();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.90F, 0.16F, 0.22F, 1.0F});
    const bool keepRunning{!ImGui::Button("X", {buttonWidth, px::ui::Scale(32.0F)})};
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::Separator();
    return keepRunning;
}

} // namespace px::desktop
