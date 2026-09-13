#include "brand_logo.h"

#include "atlas_image_loader.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace px::desktop {

std::expected<BrandLogo, std::string> BrandLogo::Load() {
    const std::string basePathText{SDL_GetBasePath() == nullptr ? "" : SDL_GetBasePath()};
    const std::filesystem::path directory{std::filesystem::path{basePathText} / "resources" / "icons" / "brand"};
    constexpr std::array<std::string_view, 3> names{"px_icon.png", "px_icon-150.png", "px_icon-200.png"};
    std::array<ImFontAtlasRectId, 3> rectIds{};
    for (std::size_t index{}; index < names.size(); ++index) {
        auto result = LoadRgbaAtlasImage(directory / names[index]);
        if (!result)
            return std::unexpected{std::move(result.error())};
        rectIds[index] = result.value();
    }
    return BrandLogo{rectIds};
}

BrandLogo::BrandLogo(std::array<ImFontAtlasRectId, 3> atlasRectIds) noexcept : atlasRectIds_{atlasRectIds} {}

void BrandLogo::Draw(const ImVec2 topLeft, const float size) const {
    Draw(*ImGui::GetWindowDrawList(), topLeft, size);
}

void BrandLogo::Draw(ImDrawList& draw, const ImVec2 topLeft, const float size) const {
    ImFontAtlas& atlas{*ImGui::GetIO().Fonts};
    ImFontAtlasRect rect{};
    if (!atlas.GetCustomRect(atlasRectIds_[AtlasDensityIndex(ImGui::GetStyle().FontScaleDpi)], &rect)) {
        return;
    }
    draw.AddImage(atlas.TexRef, topLeft, {topLeft.x + size, topLeft.y + size}, rect.uv0, rect.uv1);
}

} // namespace px::desktop
