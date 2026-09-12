#include "font_loader.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <filesystem>

namespace px::desktop {

bool ConfigureFonts() {
    const std::string basePathText{SDL_GetBasePath() == nullptr ? "" : SDL_GetBasePath()};
    const std::filesystem::path basePath{basePathText};
    const std::filesystem::path latinFont{basePath / "resources" / "fonts" / "Roboto-Regular.ttf"};
    ImGuiIO& io = ImGui::GetIO();
    constexpr float pixelSize{18.0F};
    if (!io.Fonts->AddFontFromFileTTF(latinFont.string().c_str(), pixelSize)) {
        io.Fonts->AddFontDefault();
    }

    ImFontConfig chineseConfig{};
    chineseConfig.MergeMode = true;
    chineseConfig.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", pixelSize, &chineseConfig, io.Fonts->GetGlyphRangesChineseFull());
    // Modern renderer backends build and upload the dynamic atlas during NewFrame().
    // Building it before the renderer advertises texture support produces an invalid atlas state.
    return !io.Fonts->Fonts.empty();
}

} // namespace px::desktop
