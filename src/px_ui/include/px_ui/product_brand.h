#pragma once

#include <string_view>

#include "pixels_product_version_config.h"

namespace px::ui {

[[nodiscard]] inline constexpr std::string_view ApplicationName() noexcept { return PROJECT_APPLICATION_NAME; }

[[nodiscard]] inline constexpr std::string_view WindowsProductName() noexcept { return PROJECT_PRODUCT_DISPLAY_NAME; }

[[nodiscard]] inline constexpr std::string_view StorageDirectoryName() noexcept { return PROJECT_STORAGE_DIRECTORY_NAME; }

[[nodiscard]] inline constexpr bool IsOemDistribution() noexcept { return PROJECT_IS_OEM != 0; }

}  // namespace px::ui
