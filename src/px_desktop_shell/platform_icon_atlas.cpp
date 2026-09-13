#include "px_desktop_shell/platform_icon_atlas.h"

#include "data.h"
#include "image.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace px::desktop {
namespace {

constexpr std::array<std::string_view, 4> kPlatformIconNames{"windows.png", "macos.png", "android.png", "ios.png"};

std::expected<ImFontAtlasRectId, std::string> LoadIcon(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
        return std::unexpected{"Platform icon is missing: " + path.string()};

    const std::vector<char> compressed{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    const auto image = px::Image::MakeByCompressedImage(px::Data::Copy(std::span<const char>{compressed}));
    if (!image || !image->GetData() || image->GetWidth() <= 0 || image->GetHeight() <= 0 || image->GetChannels() != 4)
        return std::unexpected{"Platform icon could not be decoded as RGBA: " + path.string()};

    ImFontAtlas& atlas{*ImGui::GetIO().Fonts};
    atlas.TexDesiredFormat = ImTextureFormat_RGBA32;
    atlas.TexPixelsUseColors = true;
    ImFontAtlasRect rect{};
    const ImFontAtlasRectId rectId{atlas.AddCustomRect(image->GetWidth(), image->GetHeight(), &rect)};
    if (rectId == ImFontAtlasRectId_Invalid || atlas.TexRef._TexData == nullptr || atlas.TexRef._TexData->BytesPerPixel != 4)
        return std::unexpected{"Dear ImGui could not allocate a platform icon atlas region"};

    ImTextureData& texture{*atlas.TexRef._TexData}; // NOLINT(gammaray-raw-pointer-boundary): Dear ImGui atlas ABI boundary.
    const auto pixels = image->GetData()->Bytes();
    const std::size_t sourcePitch{static_cast<std::size_t>(image->GetWidth()) * 4U};
    for (int row{}; row < image->GetHeight(); ++row)
        std::memcpy(texture.GetPixelsAt(rect.x, rect.y + row), pixels.data() + static_cast<std::size_t>(row) * sourcePitch, sourcePitch);
    return rectId;
}

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
    std::array<ImFontAtlasRectId, 4> rectIds{};
    for (std::size_t index{}; index < kPlatformIconNames.size(); ++index) {
        auto result = LoadIcon(directory / kPlatformIconNames[index]);
        if (!result)
            return std::unexpected{std::move(result.error())};
        rectIds[index] = result.value();
    }
    return PlatformIconAtlas{rectIds};
}

PlatformIconAtlas::PlatformIconAtlas(std::array<ImFontAtlasRectId, 4> atlasRectIds) noexcept : atlasRectIds_{atlasRectIds} {}

void PlatformIconAtlas::Draw(const px::ui::DevicePlatform platform, const ImVec2 topLeft, const float size, const ImU32 tintColor) const {
    ImFontAtlas& atlas{*ImGui::GetIO().Fonts};
    ImFontAtlasRect rect{};
    if (!atlas.GetCustomRect(atlasRectIds_[PlatformIndex(platform)], &rect))
        return;
    ImGui::GetWindowDrawList()->AddImage(atlas.TexRef, topLeft, {topLeft.x + size, topLeft.y + size}, rect.uv0, rect.uv1, tintColor);
}

} // namespace px::desktop
