#include "font_loader.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <filesystem>

namespace px::desktop {

bool ConfigureFonts(const float displayScale) {
    const std::string basePathText{SDL_GetBasePath() == nullptr ? "" : SDL_GetBasePath()};
    const std::filesystem::path basePath{basePathText};
    const std::filesystem::path latinFont{basePath / "resources" / "fonts" / "Roboto-Regular.ttf"};
    ImGuiIO& io = ImGui::GetIO();
    const float pixelSize{18.0F * displayScale};
    if (!io.Fonts->AddFontFromFileTTF(latinFont.string().c_str(), pixelSize)) {
        io.Fonts->AddFontDefault();
    }

    ImFontConfig chineseConfig{};
    chineseConfig.MergeMode = true;
    chineseConfig.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", pixelSize, &chineseConfig, io.Fonts->GetGlyphRangesChineseFull());
    return io.Fonts->Build();
}

} // namespace px::desktop
