#include "title_bar.h"

#include "window_host.h"

#include <imgui.h>

namespace px::desktop {

bool DrawTitleBar(WindowHost& window) {
    constexpr float captionButtonWidth{46.0F};
    constexpr float captionButtonCount{3.0F};
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{4.0F, 0.0F});
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Pixels");

    ImGui::SameLine(ImGui::GetWindowWidth() - captionButtonWidth * captionButtonCount - 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0F);
    if (ImGui::Button("_", ImVec2{captionButtonWidth, 32.0F})) {
        window.Minimize();
    }
    ImGui::SameLine();
    if (ImGui::Button(window.IsMaximized() ? "[]" : "[ ]", ImVec2{captionButtonWidth, 32.0F})) {
        window.ToggleMaximize();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.90F, 0.16F, 0.22F, 1.0F});
    const bool keepRunning{!ImGui::Button("X", ImVec2{captionButtonWidth, 32.0F})};
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::Separator();
    return keepRunning;
}

} // namespace px::desktop
