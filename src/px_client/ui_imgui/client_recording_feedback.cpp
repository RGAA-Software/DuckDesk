#include "client_recording_feedback.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "client_text.h"
#include "px_client_sdk/sdk_recording_session.h"
#include "px_common/file_util.h"
#include "px_common/folder_util.h"
#include "px_ui/theme_tokens.h"

namespace px::client::imgui {

ClientRecordingResult MakeRecordingResult(const px::RecordingSessionResult& result, const std::filesystem::path& directory) {
    const bool saved{!result.completed_files.empty()};
    return {.status = !result.error.empty() ? RecordingResultStatus::Failed
                      : saved               ? RecordingResultStatus::Saved
                                            : RecordingResultStatus::NoVideo,
            .directory = directory,
            .error = result.error,
            .filePath = saved ? result.completed_files.back() : std::filesystem::path{}};
}

px::ui::ToastMessage RecordingToast(const ClientRecordingResult& result, const bool english) {
    const bool saved{result.status == RecordingResultStatus::Saved};
    ClientText detailKey{ClientText::RecordingSaveFailed};
    if (saved)
        detailKey = ClientText::RecordingRevealFile;
    else if (result.status == RecordingResultStatus::NoVideo || result.error == "recording_no_keyframe")
        detailKey = ClientText::RecordingNoVideo;
    else if (result.error == "recording_queue_full")
        detailKey = ClientText::RecordingQueueFull;
    px::ui::ToastMessage message{.title = std::string{ClientTextValue(saved ? ClientText::RecordingSaved : ClientText::RecordingFailed, english)},
                                 .description = std::string{ClientTextValue(detailKey, english)},
                                 .variant = saved ? px::ui::FeedbackVariant::Success : px::ui::FeedbackVariant::Error,
                                 .duration = std::chrono::seconds{6}};
    std::error_code directoryError{};
    const auto savedFile = result.filePath.empty() ? std::filesystem::path{} : std::filesystem::absolute(result.filePath, directoryError);
    if (!result.filePath.empty() && !directoryError && std::filesystem::is_regular_file(savedFile, directoryError)) {
        const auto encodedFile = savedFile.u8string();
        message.description += "\n" + std::string{encodedFile.begin(), encodedFile.end()};
        if (!saved) message.description += "\n" + std::string{ClientTextValue(ClientText::RecordingRevealFile, english)};
        message.onClick = [savedFile] { px::FileUtil::SelectFileInExplorer(savedFile); };
    } else if (!result.directory.empty() && std::filesystem::is_directory(result.directory, directoryError)) {
        const auto encodedDirectory = result.directory.u8string();
        message.description += "\n" + std::string{encodedDirectory.begin(), encodedDirectory.end()};
        message.description += "\n" + std::string{ClientTextValue(ClientText::RecordingOpenFolder, english)};
        message.onClick = [directory = result.directory] { px::FolderUtil::OpenDir(directory); };
    }
    return message;
}

float RecordingPulseOpacity(const double elapsedSeconds) noexcept {
    if (!std::isfinite(elapsedSeconds)) return 1.0F;
    constexpr double periodSeconds{2.0};
    const double phase{std::max(0.0, elapsedSeconds) * 2.0 * std::numbers::pi / periodSeconds};
    return static_cast<float>(0.45 + 0.55 * (0.5 + 0.5 * std::cos(phase)));
}

float DrawRecordingIndicator(const bool active, const ControllerArea& contentArea, const double elapsedSeconds, const bool english) {
    if (!active) return 0.0F;
    const auto metrics = px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi);
    const auto tokens = px::ui::CurrentThemeTokens();
    const auto label = ClientTextValue(ClientText::RecordingIndicator, english);
    const ImVec2 textSize{ImGui::CalcTextSize(label.data(), label.data() + label.size())};
    const float margin{metrics.spacingLg};
    const float dotRadius{metrics.scale * 3.0F};
    const float totalWidth{textSize.x + dotRadius * 2.0F + metrics.spacingSm};
    const float left{std::max(contentArea.left + margin, contentArea.left + contentArea.width - margin - totalWidth)};
    const float top{contentArea.top + margin};
    const ImVec2 textPosition{left + dotRadius * 2.0F + metrics.spacingSm, top};
    ImVec4 foreground{tokens.videoRecordingForeground};
    foreground.w *= RecordingPulseOpacity(elapsedSeconds);
    ImVec4 outline{tokens.videoOverlayOutline};
    outline.w *= foreground.w;
    ImDrawList& draw{*ImGui::GetForegroundDrawList()};
    draw.PushClipRect({contentArea.left, contentArea.top}, {contentArea.left + contentArea.width, contentArea.top + contentArea.height}, true);
    draw.AddText({textPosition.x + metrics.scale, textPosition.y + metrics.scale}, ImGui::GetColorU32(outline), label.data(),
                 label.data() + label.size());
    draw.AddText(textPosition, ImGui::GetColorU32(foreground), label.data(), label.data() + label.size());
    draw.AddCircleFilled({left + dotRadius, top + textSize.y * 0.5F}, dotRadius, ImGui::GetColorU32(foreground));
    draw.PopClipRect();
    return textSize.y + metrics.spacingSm;
}

}  // namespace px::client::imgui
