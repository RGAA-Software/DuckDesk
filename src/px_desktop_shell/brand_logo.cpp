#include "brand_logo.h"

#include "data.h"
#include "image.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace px::desktop {

std::expected<BrandLogo, std::string> BrandLogo::Load() {
    const std::string basePathText{SDL_GetBasePath() == nullptr ? "" : SDL_GetBasePath()};
    const std::filesystem::path logoPath{std::filesystem::path{basePathText} / "resources" / "icons" / "px_icon.png"};
    std::ifstream stream{logoPath, std::ios::binary};
    if (!stream) {
        return std::unexpected{"Pixels PNG logo is missing: " + logoPath.string()};
    }
    const std::vector<char> compressed{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    const auto image = px::Image::MakeByCompressedImage(px::Data::Copy(std::span<const char>{compressed}));
    if (!image || !image->GetData() || image->GetWidth() <= 0 || image->GetHeight() <= 0 || image->GetChannels() != 4) {
        return std::unexpected{"Pixels PNG logo could not be decoded as RGBA"};
    }

    ImFontAtlas& atlas{*ImGui::GetIO().Fonts};
    atlas.TexDesiredFormat = ImTextureFormat_RGBA32;
    atlas.TexPixelsUseColors = true;
    ImFontAtlasRect rect{};
    const ImFontAtlasRectId rectId{atlas.AddCustomRect(image->GetWidth(), image->GetHeight(), &rect)};
    if (rectId == ImFontAtlasRectId_Invalid || atlas.TexRef._TexData == nullptr || atlas.TexRef._TexData->BytesPerPixel != 4) {
        return std::unexpected{"Dear ImGui could not allocate the Pixels logo atlas region"};
    }

    ImTextureData& texture{*atlas.TexRef._TexData}; // NOLINT(gammaray-raw-pointer-boundary): Dear ImGui atlas ABI boundary.
    const auto pixels = image->GetData()->Bytes();
    const std::size_t sourcePitch{static_cast<std::size_t>(image->GetWidth()) * 4U};
    for (int row{}; row < image->GetHeight(); ++row) {
        std::memcpy(texture.GetPixelsAt(rect.x, rect.y + row), pixels.data() + static_cast<std::size_t>(row) * sourcePitch, sourcePitch);
    }
    return BrandLogo{rectId};
}

BrandLogo::BrandLogo(const ImFontAtlasRectId atlasRectId) noexcept : atlasRectId_{atlasRectId} {}

void BrandLogo::Draw(const ImVec2 topLeft, const float size) const {
    ImFontAtlas& atlas{*ImGui::GetIO().Fonts};
    ImFontAtlasRect rect{};
    if (!atlas.GetCustomRect(atlasRectId_, &rect)) {
        return;
    }
    ImGui::GetWindowDrawList()->AddImage(atlas.TexRef, topLeft, {topLeft.x + size, topLeft.y + size}, rect.uv0, rect.uv1);
}

} // namespace px::desktop
