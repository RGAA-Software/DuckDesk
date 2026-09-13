#include "px_desktop_shell/platform_icon_atlas.h"

#include "atlas_image_loader.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace px::desktop {
namespace {

constexpr std::array<std::array<std::string_view, 3>, 4> kPlatformIconNames{{
    {"windows.png", "windows-150.png", "windows-200.png"},
    {"macos.png", "macos-150.png", "macos-200.png"},
    {"android.png", "android-150.png", "android-200.png"},
    {"ios.png", "ios-150.png", "ios-200.png"},
}};

std::size_t PlatformIndex(const px::ui::DevicePlatform platform) noexcept {
    switch (platform) {
    case px::ui::DevicePlatform::MacOS:
        return 1;
    case px::ui::DevicePlatform::Android:
        return 2;
    case px::ui::DevicePlatform::IOS:
        return 3;
    case px::ui::DevicePlatform::Unknown:
    case px::ui::DevicePlatform::Windows:
        return 0;
    }
    return 0;
}

} // namespace

std::expected<PlatformIconAtlas, std::string> PlatformIconAtlas::Load() {
    const std::string basePathText{SDL_GetBasePath() == nullptr ? "" : SDL_GetBasePath()};
    const std::filesystem::path directory{std::filesystem::path{basePathText} / "resources" / "icons" / "platform"};
    std::array<ImFontAtlasRectId, 12> rectIds{};
    for (std::size_t platformIndex{}; platformIndex < kPlatformIconNames.size(); ++platformIndex) {
        for (std::size_t densityIndex{}; densityIndex < kPlatformIconNames[platformIndex].size(); ++densityIndex) {
            auto result = LoadRgbaAtlasImage(directory / kPlatformIconNames[platformIndex][densityIndex]);
            if (!result)
                return std::unexpected{std::move(result.error())};
            rectIds[platformIndex * 3 + densityIndex] = result.value();
        }
    }
    return PlatformIconAtlas{rectIds};
}

PlatformIconAtlas::PlatformIconAtlas(std::array<ImFontAtlasRectId, 12> atlasRectIds) noexcept : atlasRectIds_{atlasRectIds} {}

void PlatformIconAtlas::Draw(const px::ui::DevicePlatform platform, const ImVec2 topLeft, const float size, const ImU32 tintColor) const {
    ImFontAtlas& atlas{*ImGui::GetIO().Fonts};
    ImFontAtlasRect rect{};
    const std::size_t index{PlatformIndex(platform) * 3 + AtlasDensityIndex(ImGui::GetStyle().FontScaleDpi)};
    if (!atlas.GetCustomRect(atlasRectIds_[index], &rect))
        return;
    ImGui::GetWindowDrawList()->AddImage(atlas.TexRef, topLeft, {topLeft.x + size, topLeft.y + size}, rect.uv0, rect.uv1, tintColor);
}

} // namespace px::desktop
