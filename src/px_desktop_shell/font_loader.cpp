#include "font_loader.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <filesystem>

namespace px::desktop {

bool ConfigureFonts() {
    const std::string basePathText{SDL_GetBasePath() == nullptr ? "" : SDL_GetBasePath()};
    const std::filesystem::path basePath{basePathText};
    const std::filesystem::path latinFont{basePath / "resources" / "fonts" / "Roboto-Regular.ttf"};
    const std::filesystem::path mediumFont{basePath / "resources" / "fonts" / "Roboto-Medium.ttf"};
    ImGuiIO& io = ImGui::GetIO();
    // FontScaleDpi applies the monitor scale at runtime. Keep the atlas at the
    // 960 x 640 logical design size so 150% displays do not scale the type twice.
    constexpr float pixelSize{15.0F};
    if (!io.Fonts->AddFontFromFileTTF(latinFont.string().c_str(), pixelSize)) {
        io.Fonts->AddFontDefault();
    }

    ImFontConfig chineseConfig{};
    chineseConfig.MergeMode = true;
    chineseConfig.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", pixelSize, &chineseConfig, io.Fonts->GetGlyphRangesChineseFull());
    if (!io.Fonts->AddFontFromFileTTF(mediumFont.string().c_str(), pixelSize)) {
        return false;
    }
    // Modern renderer backends build and upload the dynamic atlas during NewFrame().
    // Building it before the renderer advertises texture support produces an invalid atlas state.
    return !io.Fonts->Fonts.empty();
}

} // namespace px::desktop
