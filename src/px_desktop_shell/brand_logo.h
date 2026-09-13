#pragma once

#include <array>
#include <expected>
#include <string>

#include <imgui.h>

namespace px::desktop {

class BrandLogo final {
  public:
    static std::expected<BrandLogo, std::string> Load();

    void Draw(ImVec2 topLeft, float size) const;
    void Draw(ImDrawList& draw, ImVec2 topLeft, float size) const;

  private:
    explicit BrandLogo(std::array<ImFontAtlasRectId, 3> atlasRectIds) noexcept;

    std::array<ImFontAtlasRectId, 3> atlasRectIds_{};
};

} // namespace px::desktop
