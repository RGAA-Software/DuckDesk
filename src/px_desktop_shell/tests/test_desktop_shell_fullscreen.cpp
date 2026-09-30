#include <imgui.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

#include "../title_bar.h"
#include "px_desktop_shell/desktop_shell.h"
#include "px_ui/layout_metrics.h"

namespace {

struct FullscreenExercise final {
    int frameCount{};
    int transitionCount{};
    bool succeeded{true};
    bool expectedFullscreen{};
};

bool ExerciseFullscreen(const bool preferVulkan) {
    auto creation = px::desktop::DesktopShell::Create({.title = "Pixels fullscreen regression",
                                                       .width = 960,
                                                       .height = 640,
                                                       .continuousRendering = true,
                                                       .edgeToEdgeContent = true,
                                                       .preferVulkanVideo = preferVulkan});
    if (!creation) {
        std::cerr << "Desktop shell creation failed: " << creation.error() << '\n';
        return false;
    }
    auto shell = std::make_shared<px::desktop::DesktopShell>(std::move(creation.value()));
    const auto exercise = std::make_shared<FullscreenExercise>();
    const std::array<std::uint8_t, 64U * 64U * 4U> pixels{};
    if (!shell->UpdateVideoTexture(64, 64, pixels) || shell->VideoTextureId() == 0) {
        std::cerr << "Video texture initialization failed\n";
        return false;
    }
    const int runResult = shell->Run([shell, exercise] {
        ++exercise->frameCount;
        const bool fullscreen{shell->IsFullscreen()};
        const ImGuiViewport& viewport{*ImGui::GetMainViewport()};
        const float titleBarHeight{fullscreen ? 0.0F : px::ui::Scale(static_cast<float>(px::desktop::kTitleBarLogicalHeight))};
        const ImVec2 contentPosition{ImGui::GetCursorScreenPos()};
        const ImVec2 contentSize{ImGui::GetContentRegionAvail()};
        const bool fillsContent{
            std::abs(contentPosition.x - viewport.Pos.x) < 0.1F && std::abs(contentPosition.y - viewport.Pos.y - titleBarHeight) < 0.1F &&
            std::abs(contentSize.x - viewport.Size.x) < 0.1F && std::abs(contentSize.y - viewport.Size.y + titleBarHeight) < 0.1F};
        if (!fillsContent || fullscreen != exercise->expectedFullscreen) {
            std::cerr << "Fullscreen content/state mismatch: fullscreen=" << fullscreen << ", position=" << contentPosition.x << ','
                      << contentPosition.y << ", size=" << contentSize.x << ',' << contentSize.y << ", viewport=" << viewport.Size.x << ','
                      << viewport.Size.y << '\n';
            exercise->succeeded = false;
        }
        ImGui::Image(static_cast<ImTextureID>(shell->VideoTextureId()), contentSize);
        if (exercise->frameCount % 15 == 0 && exercise->transitionCount < 4) {
            exercise->succeeded = shell->ToggleFullscreen() && exercise->succeeded;
            exercise->expectedFullscreen = !exercise->expectedFullscreen;
            ++exercise->transitionCount;
        }
        if (!exercise->succeeded || exercise->frameCount >= 80) shell->RequestExit();
    });
    return runResult == 0 && exercise->succeeded && exercise->transitionCount == 4;
}

}  // namespace

int main() {
    for (const bool preferVulkan : {true, false}) {
        for (int lifecycleIndex{}; lifecycleIndex < 2; ++lifecycleIndex) {
            if (!ExerciseFullscreen(preferVulkan)) {
                std::cerr << "Fullscreen lifecycle failed: Vulkan=" << preferVulkan << ", lifecycle=" << lifecycleIndex << '\n';
                return 1;
            }
        }
    }
    std::cout << "Repeated Vulkan/D3D11 fullscreen transitions, edge-to-edge content and shell destruction passed\n";
    return 0;
}
