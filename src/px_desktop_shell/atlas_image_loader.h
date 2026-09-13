#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include <imgui.h>

namespace px::desktop {

[[nodiscard]] std::expected<ImFontAtlasRectId, std::string> LoadRgbaAtlasImage(const std::filesystem::path& path);
[[nodiscard]] std::size_t AtlasDensityIndex(float displayScale) noexcept;

} // namespace px::desktop
