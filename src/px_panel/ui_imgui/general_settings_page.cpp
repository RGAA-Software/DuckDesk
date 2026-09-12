#include "general_settings_page.h"

#include <imgui.h>

#include <array>
#include <utility>

namespace px::panel::ui {
namespace {

px::ui::TextId ResultText(const GeneralSaveResult result) {
    switch (result) {
    case GeneralSaveResult::InvalidBitrate:
        return px::ui::TextId::InvalidBitrate;
    case GeneralSaveResult::InvalidFrameRate:
        return px::ui::TextId::InvalidFrameRate;
    case GeneralSaveResult::InvalidResolution:
        return px::ui::TextId::InvalidResolution;
    case GeneralSaveResult::UnsupportedResolution:
        return px::ui::TextId::UnsupportedResolution;
    case GeneralSaveResult::InvalidAspectRatio:
        return px::ui::TextId::InvalidAspectRatio;
    case GeneralSaveResult::Saved:
        return px::ui::TextId::Saved;
    }
    return px::ui::TextId::OperationFailed;
}

} // namespace

GeneralSettingsPage::GeneralSettingsPage(std::shared_ptr<SettingsPort> port) : port_{std::move(port)} {}

void GeneralSettingsPage::Reload() {
    draft_ = port_->Snapshot().general;
    loaded_ = true;
}

void GeneralSettingsPage::Draw(const px::ui::Localizer& localizer) {
    if (!loaded_) {
        Reload();
    }
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::EncoderSettings).data());
    ImGui::Separator();
    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputInt(localizer.Text(px::ui::TextId::BitrateMbps).data(), &draft_.bitrateMbps);
    constexpr std::array frameRates{15, 30, 60, 90, 120, 144};
    if (ImGui::BeginCombo(localizer.Text(px::ui::TextId::FrameRate).data(), std::to_string(draft_.frameRate).c_str())) {
        for (const int frameRate : frameRates) {
            if (ImGui::Selectable(std::to_string(frameRate).c_str(), frameRate == draft_.frameRate)) {
                draft_.frameRate = frameRate;
            }
        }
        ImGui::EndCombo();
    }
    int codec{draft_.codec == VideoCodec::H265 ? 1 : 0};
    const std::array codecNames{"H.264", "H.265 (HEVC)"};
    if (ImGui::Combo(localizer.Text(px::ui::TextId::VideoCodec).data(), &codec, codecNames.data(), static_cast<int>(codecNames.size()))) {
        draft_.codec = codec == 1 ? VideoCodec::H265 : VideoCodec::H264;
    }
    ImGui::Checkbox(localizer.Text(px::ui::TextId::ResizeResolution).data(), &draft_.resizeEnabled);
    if (!draft_.resizeEnabled) {
        ImGui::BeginDisabled();
    }
    ImGui::InputInt(localizer.Text(px::ui::TextId::Width).data(), &draft_.width);
    ImGui::InputInt(localizer.Text(px::ui::TextId::Height).data(), &draft_.height);
    if (!draft_.resizeEnabled) {
        ImGui::EndDisabled();
    }
    ImGui::Checkbox(localizer.Text(px::ui::TextId::CaptureAudio).data(), &draft_.captureAudio);
    if (ImGui::Button(localizer.Text(px::ui::TextId::Save).data())) {
        lastResult_ = port_->SaveGeneral(draft_);
        showRestartPrompt_ = lastResult_ == GeneralSaveResult::Saved;
    }
    if (lastResult_ != GeneralSaveResult::Saved) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{0.9F, 0.28F, 0.25F, 1.0F}, "%s", localizer.Text(ResultText(lastResult_)).data());
    }
    if (showRestartPrompt_) {
        ImGui::OpenPopup("RestartRenderAfterGeneralSave");
        showRestartPrompt_ = false;
    }
    if (ImGui::BeginPopupModal("RestartRenderAfterGeneralSave", {}, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(localizer.Text(px::ui::TextId::RestartRenderPrompt).data());
        if (ImGui::Button(localizer.Text(px::ui::TextId::RestartNow).data())) {
            port_->RestartRender();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(localizer.Text(px::ui::TextId::Later).data())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace px::panel::ui
