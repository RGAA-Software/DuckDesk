#pragma once

#include "px_ui/device_platform.h"

#include <array>
#include <expected>
#include <string>

#include <imgui.h>

namespace px::desktop {

class PlatformIconAtlas final {
  public:
    static std::expected<PlatformIconAtlas, std::string> Load();

    void Draw(px::ui::DevicePlatform platform, ImVec2 topLeft, float size, ImU32 tintColor) const;

  private:
    explicit PlatformIconAtlas(std::array<ImFontAtlasRectId, 4> atlasRectIds) noexcept;

    std::array<ImFontAtlasRectId, 4> atlasRectIds_{};
};

} // namespace px::desktop
