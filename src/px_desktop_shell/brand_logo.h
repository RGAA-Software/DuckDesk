#pragma once

#include <expected>
#include <string>

#include <imgui.h>

namespace px::desktop {

class BrandLogo final {
  public:
    static std::expected<BrandLogo, std::string> Load();

    void Draw(ImVec2 topLeft, float size) const;

  private:
    explicit BrandLogo(ImFontAtlasRectId atlasRectId) noexcept;

    ImFontAtlasRectId atlasRectId_{ImFontAtlasRectId_Invalid};
};

} // namespace px::desktop
