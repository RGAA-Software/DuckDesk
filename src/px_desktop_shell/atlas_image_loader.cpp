#include "atlas_image_loader.h"

#include "data.h"
#include "image.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

namespace px::desktop {

std::expected<ImFontAtlasRectId, std::string> LoadRgbaAtlasImage(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
        return std::unexpected{"Desktop image is missing: " + path.string()};

    const std::vector<char> compressed{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    const auto image = px::Image::MakeByCompressedImage(px::Data::Copy(std::span<const char>{compressed}));
    if (!image || !image->GetData() || image->GetWidth() <= 0 || image->GetHeight() <= 0 || image->GetChannels() != 4)
        return std::unexpected{"Desktop image could not be decoded as RGBA: " + path.string()};

    ImFontAtlas& atlas{*ImGui::GetIO().Fonts};
    atlas.TexDesiredFormat = ImTextureFormat_RGBA32;
    atlas.TexPixelsUseColors = true;
    ImFontAtlasRect rect{};
    const ImFontAtlasRectId rectId{atlas.AddCustomRect(image->GetWidth(), image->GetHeight(), &rect)};
    if (rectId == ImFontAtlasRectId_Invalid || atlas.TexRef._TexData == nullptr || atlas.TexRef._TexData->BytesPerPixel != 4)
        return std::unexpected{"Dear ImGui could not allocate an image atlas region: " + path.string()};

    ImTextureData& texture{*atlas.TexRef._TexData}; // NOLINT(gammaray-raw-pointer-boundary): Dear ImGui atlas ABI boundary.
    const auto pixels = image->GetData()->Bytes();
    const std::size_t sourcePitch{static_cast<std::size_t>(image->GetWidth()) * 4U};
    for (int row{}; row < image->GetHeight(); ++row)
        std::memcpy(texture.GetPixelsAt(rect.x, rect.y + row), pixels.data() + static_cast<std::size_t>(row) * sourcePitch, sourcePitch);
    return rectId;
}

std::size_t AtlasDensityIndex(const float displayScale) noexcept {
    if (displayScale < 1.25F)
        return 0;
    if (displayScale < 1.75F)
        return 1;
    return 2;
}

} // namespace px::desktop
