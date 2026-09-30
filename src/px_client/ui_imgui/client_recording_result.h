#pragma once

#include <filesystem>
#include <string>

namespace px {
struct RecordingSessionResult;
}

namespace px::client::imgui {

enum class RecordingResultStatus { Saved, NoVideo, Failed };

struct ClientRecordingResult final {
    RecordingResultStatus status{RecordingResultStatus::Failed};
    std::filesystem::path directory{};
    std::string error{};
    std::filesystem::path filePath{};
};

[[nodiscard]] ClientRecordingResult MakeRecordingResult(const px::RecordingSessionResult& result, const std::filesystem::path& directory);

}  // namespace px::client::imgui
