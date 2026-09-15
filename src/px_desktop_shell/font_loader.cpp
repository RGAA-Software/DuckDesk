#include "font_loader.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <misc/freetype/imgui_freetype.h>

#include <filesystem>

namespace px::desktop {

bool ConfigureFonts() {
    const std::string basePathText{SDL_GetBasePath() == nullptr ? "" : SDL_GetBasePath()};
    const std::filesystem::path basePath{basePathText};
    const std::filesystem::path latinFont{basePath / "resources" / "fonts" / "Roboto-Regular.ttf"};
    const std::filesystem::path mediumFont{basePath / "resources" / "fonts" / "Roboto-Medium.ttf"};
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader()); // NOLINT(pixels-raw-pointer-boundary): Dear ImGui loader ABI boundary.
    // ImGui 1.92 dynamically rasterizes this logical size at the current framebuffer density.
    // Sixteen pixels keeps body text readable at 1080p without enlarging the surrounding controls.
    constexpr float pixelSize{16.0F};
    ImFontConfig regularConfig{};
    regularConfig.RasterizerMultiply = 1.06F;
    if (!io.Fonts->AddFontFromFileTTF(latinFont.string().c_str(), pixelSize, &regularConfig)) {
        io.Fonts->AddFontDefault();
    }

    ImFontConfig chineseConfig{};
    chineseConfig.MergeMode = true;
    chineseConfig.RasterizerMultiply = 1.04F;
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", pixelSize, &chineseConfig, io.Fonts->GetGlyphRangesChineseFull());
    ImFontConfig mediumConfig{};
    mediumConfig.RasterizerMultiply = 1.04F;
    if (!io.Fonts->AddFontFromFileTTF(mediumFont.string().c_str(), pixelSize, &mediumConfig)) {
        return false;
    }
    ImFontConfig chineseMediumConfig{};
    chineseMediumConfig.MergeMode = true;
    chineseMediumConfig.RasterizerMultiply = 1.04F;
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", pixelSize, &chineseMediumConfig, io.Fonts->GetGlyphRangesChineseFull());
    // Modern renderer backends build and upload the dynamic atlas during NewFrame().
    // Building it before the renderer advertises texture support produces an invalid atlas state.
    return !io.Fonts->Fonts.empty();
}

} // namespace px::desktop
