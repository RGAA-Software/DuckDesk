#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "client_video_frame.h"

namespace px::client::imgui {

enum class ScreenshotStatus { Saved, NoFrame, ReadbackFailed, DirectoryUnavailable, WriteFailed };

struct ScreenshotResult final {
    ScreenshotStatus status{ScreenshotStatus::NoFrame};
    std::filesystem::path path{};
    std::string error{};
};

[[nodiscard]] std::optional<std::filesystem::path> DefaultScreenshotDirectory();
[[nodiscard]] ScreenshotResult SaveScreenshotFile(const std::shared_ptr<ClientVideoFrame>& frame, const std::filesystem::path& path);

}  // namespace px::client::imgui
