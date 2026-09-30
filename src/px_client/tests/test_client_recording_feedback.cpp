#include <Windows.h>
#include <gtest/gtest.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>

#include "client_recording_feedback.h"
#include "client_text.h"
#include "px_client_sdk/sdk_recording_session.h"
#include "px_ui/px_ui_theme.h"
#include "px_ui/theme_tokens.h"

namespace px::client::imgui {
namespace {

TEST(ClientRecordingFeedback, SuccessRequiresFinalizedFilesNotMerelyVideoPackets) {
    const std::filesystem::path directory{L"C:\\Pixels 录屏"};
    const auto recordingFile = directory / L"已保存.mp4";
    const auto finalized =
        MakeRecordingResult({.directories = {"writer-directory"}, .video_packets = 10, .completed_files = {recordingFile}}, directory);
    EXPECT_EQ(finalized.status, RecordingResultStatus::Saved);
    EXPECT_EQ(finalized.directory, directory);
    EXPECT_EQ(finalized.filePath, recordingFile);
    const auto noFiles = MakeRecordingResult({.video_packets = 10}, directory);
    EXPECT_EQ(noFiles.status, RecordingResultStatus::NoVideo);
    EXPECT_TRUE(noFiles.filePath.empty());
    EXPECT_EQ(MakeRecordingResult({.directories = {"writer-directory"}}, directory).status, RecordingResultStatus::NoVideo);
    const auto failedFinalize = MakeRecordingResult({.directories = {"writer-directory"}, .error = "recording_finalize_failed"}, directory);
    EXPECT_EQ(failedFinalize.status, RecordingResultStatus::Failed);
    EXPECT_EQ(failedFinalize.error, "recording_finalize_failed");
}

TEST(ClientRecordingFeedback, RollingRecordingSelectsItsLastFinalizedSegmentNotAnUnrelatedDirectoryFile) {
    const std::filesystem::path directory{L"C:\\Pixels 录屏"};
    const auto firstSegment = directory / L"segment-one.mp4";
    const auto lastSegment = directory / L"segment-two.mp4";
    const auto finalized = MakeRecordingResult({.completed_files = {firstSegment, lastSegment}}, directory);
    EXPECT_EQ(finalized.filePath, lastSegment);
}

TEST(ClientRecordingFeedback, ToastReportsFailureAndNoVideoInBothLanguages) {
    for (const bool english : {false, true}) {
        EXPECT_EQ(RecordingToast({.status = RecordingResultStatus::Saved}, english).title, ClientTextValue(ClientText::RecordingSaved, english));
        const auto noVideo = RecordingToast({.status = RecordingResultStatus::NoVideo}, english);
        EXPECT_EQ(noVideo.variant, px::ui::FeedbackVariant::Error);
        EXPECT_EQ(noVideo.description, ClientTextValue(ClientText::RecordingNoVideo, english));
        const auto noKeyframe = RecordingToast({.error = "recording_no_keyframe"}, english);
        EXPECT_EQ(noKeyframe.description, ClientTextValue(ClientText::RecordingNoVideo, english));
        const auto overflow = RecordingToast({.error = "recording_queue_full"}, english);
        EXPECT_EQ(overflow.description, ClientTextValue(ClientText::RecordingQueueFull, english));
        const auto writeFailure = RecordingToast({.error = "writer_technical_error"}, english);
        EXPECT_EQ(writeFailure.description, ClientTextValue(ClientText::RecordingSaveFailed, english));
        EXPECT_FALSE(writeFailure.onClick);
    }
}

TEST(ClientRecordingFeedback, ExistingUnicodeDirectoryMakesCompletionToastActionable) {
    const std::filesystem::path directory{std::filesystem::temp_directory_path() /
                                          ("pixels-recording-toast-test-" + std::to_string(GetCurrentProcessId()) + "-" +
                                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) /
                                          L"录屏 测试"};
    std::filesystem::create_directories(directory);
    const auto encodedDirectory = directory.u8string();
    for (const auto status : {RecordingResultStatus::Saved, RecordingResultStatus::Failed}) {
        const auto message = RecordingToast({.status = status, .directory = directory}, false);
        EXPECT_TRUE(message.onClick);
        EXPECT_NE(message.description.find(std::string{encodedDirectory.begin(), encodedDirectory.end()}), std::string::npos);
        EXPECT_NE(message.description.find(ClientTextValue(ClientText::RecordingOpenFolder, false)), std::string::npos);
    }
    const auto savedFile = directory / L"录像, 已保存.mp4";
    { std::ofstream recordingFile{savedFile}; }
    const auto encodedFile = savedFile.u8string();
    const auto savedMessage = RecordingToast({.status = RecordingResultStatus::Saved, .directory = directory, .filePath = savedFile}, false);
    EXPECT_TRUE(savedMessage.onClick);
    EXPECT_NE(savedMessage.description.find(std::string{encodedFile.begin(), encodedFile.end()}), std::string::npos);
    EXPECT_NE(savedMessage.description.find(ClientTextValue(ClientText::RecordingRevealFile, false)), std::string::npos);
    std::error_code cleanupError{};
    std::filesystem::remove_all(directory.parent_path(), cleanupError);
}

TEST(ClientRecordingFeedback, BreathingIsSmoothPeriodicAndNeverDisappears) {
    EXPECT_FLOAT_EQ(RecordingPulseOpacity(0.0), 1.0F);
    EXPECT_FLOAT_EQ(RecordingPulseOpacity(0.5), 0.725F);
    EXPECT_FLOAT_EQ(RecordingPulseOpacity(1.0), 0.45F);
    EXPECT_FLOAT_EQ(RecordingPulseOpacity(2.0), 1.0F);
    EXPECT_NEAR(RecordingPulseOpacity(0.7), RecordingPulseOpacity(2.7), 0.00001F);
    EXPECT_NEAR(RecordingPulseOpacity(0.7), RecordingPulseOpacity(0.7001), 0.0002F);
    EXPECT_FLOAT_EQ(RecordingPulseOpacity(-1.0), 1.0F);
    EXPECT_FLOAT_EQ(RecordingPulseOpacity(std::numeric_limits<double>::quiet_NaN()), 1.0F);
}

TEST(ClientRecordingFeedback, IndicatorStaysBelowTitleBarUsesRedAndReservesToastSpaceWithoutAnInputWindow) {
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = {1280.0F, 720.0F};
    ImGui::GetIO().DeltaTime = 1.0F / 60.0F;
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetIO().Fonts->AddFontDefault();
    for (const auto theme : {px::ui::Theme::Light, px::ui::Theme::Dark}) {
        for (const float scale : {1.0F, 1.5F, 2.0F}) {
            px::ui::ApplyPixelsTheme(theme, scale);
            const ControllerArea area{0.0F, 40.0F, 1280.0F, 680.0F, 40.0F};
            unsigned int brightAlpha{};
            unsigned int dimAlpha{};
            for (const double elapsedSeconds : {0.0, 1.0}) {
                ImGui::NewFrame();
                EXPECT_FLOAT_EQ(DrawRecordingIndicator(false, area, elapsedSeconds, true), 0.0F);
                ImDrawList& draw{*ImGui::GetForegroundDrawList()};
                EXPECT_EQ(draw.VtxBuffer.Size, 0);
                const float occupiedHeight{DrawRecordingIndicator(true, area, elapsedSeconds, true)};
                EXPECT_GT(occupiedHeight, ImGui::GetTextLineHeight());
                EXPECT_GT(draw.VtxBuffer.Size, 0);
                unsigned int maximumRedAlpha{};
                for (const auto& vertex : draw.VtxBuffer) {
                    EXPECT_GE(vertex.pos.y, area.top);
                    EXPECT_GT(vertex.pos.x, area.width * 0.5F);
                    EXPECT_LE(vertex.pos.x, area.width);
                    const auto red = (vertex.col >> IM_COL32_R_SHIFT) & 0xffU;
                    const auto green = (vertex.col >> IM_COL32_G_SHIFT) & 0xffU;
                    const auto blue = (vertex.col >> IM_COL32_B_SHIFT) & 0xffU;
                    if (red > green && red > blue) maximumRedAlpha = std::max(maximumRedAlpha, (vertex.col >> IM_COL32_A_SHIFT) & 0xffU);
                }
                if (elapsedSeconds == 0.0)
                    brightAlpha = maximumRedAlpha;
                else
                    dimAlpha = maximumRedAlpha;
                // No window/widget is created by foreground drawing; it cannot intercept remote mouse input.
                EXPECT_FALSE(ImGui::IsAnyItemHovered());
                ImGui::EndFrame();
            }
            EXPECT_GT(brightAlpha, dimAlpha);
            EXPECT_GT(dimAlpha, 0U);
        }
    }
    ImGui::DestroyContext();
}

}  // namespace
}  // namespace px::client::imgui
