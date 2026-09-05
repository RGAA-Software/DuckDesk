#ifndef PX_COMMON_NEW_PATH_CODEC_H
#define PX_COMMON_NEW_PATH_CODEC_H

#include <filesystem>
#include <string>
#include <string_view>

#include "async_result.h"

namespace px {

[[nodiscard]] bool IsValidUtf8(std::string_view value) noexcept;
[[nodiscard]] PxResult<std::filesystem::path> PathFromUtf8(std::string_view utf8);
[[nodiscard]] PxResult<std::string> PathToUtf8(const std::filesystem::path& path);

} // namespace px

#endif // PX_COMMON_NEW_PATH_CODEC_H
