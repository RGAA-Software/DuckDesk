#include "controller_settings_page.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace px::panel::ui {

ControllerSettingsPage::ControllerSettingsPage(std::shared_ptr<SettingsPort> port) : port_{std::move(port)} {}

void ControllerSettingsPage::Reload() {
    draft_ = port_->Snapshot().controller;
    const auto length = std::min(draft_.recordingPath.size(), recordingPath_.size() - 1);
    std::memcpy(recordingPath_.data(), draft_.recordingPath.data(), length);
    recordingPath_[length] = '\0';
    loaded_ = true;
}

void ControllerSettingsPage::Draw(const px::ui::Localizer& localizer) {
    if (!loaded_) {
        Reload();
    }
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::ClientWindowSettings).data());
    ImGui::Separator();
    ImGui::Checkbox(localizer.Text(px::ui::TextId::MaximizeClient).data(), &draft_.maximizeClient);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::DisplayClientLogo).data(), &draft_.displayClientLogo);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::ColorfulTitleBar).data(), &draft_.colorfulTitleBar);
    constexpr std::array screenCounts{2, 4, 6, 8};
    if (ImGui::BeginCombo(localizer.Text(px::ui::TextId::MaximumScreens).data(), std::to_string(draft_.maximumScreens).c_str())) {
        for (const int count : screenCounts) {
            if (ImGui::Selectable(std::to_string(count).c_str(), count == draft_.maximumScreens)) {
                draft_.maximumScreens = count;
            }
        }
        ImGui::EndCombo();
    }
    int decoder{static_cast<int>(draft_.preferredDecoder)};
    const std::array decoderNames{localizer.Text(px::ui::TextId::Automatic).data(), localizer.Text(px::ui::TextId::HardwareDecoder).data(),
                                  localizer.Text(px::ui::TextId::SoftwareDecoder).data()};
    if (ImGui::Combo(localizer.Text(px::ui::TextId::PreferredDecoder).data(), &decoder, decoderNames.data(),
                     static_cast<int>(decoderNames.size()))) {
        draft_.preferredDecoder = static_cast<PreferredDecoder>(decoder);
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputText(localizer.Text(px::ui::TextId::RecordingPath).data(), recordingPath_.data(), recordingPath_.size());
    if (ImGui::Button(localizer.Text(px::ui::TextId::Save).data())) {
        draft_.recordingPath = recordingPath_.data();
        port_->SaveController(draft_);
    }
}

} // namespace px::panel::ui
