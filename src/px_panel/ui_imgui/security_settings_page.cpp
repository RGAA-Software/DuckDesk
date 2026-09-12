#include "security_settings_page.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace px::panel::ui {

SecuritySettingsPage::SecuritySettingsPage(std::shared_ptr<SettingsPort> port) : port_{std::move(port)} {}

void SecuritySettingsPage::Draw(const px::ui::Localizer& localizer) {
    if (!loaded_) {
        const auto state = port_->Snapshot();
        disconnectAutoLock_ = state.disconnectAutoLock;
        const auto length = std::min(state.logDestination.size(), logDestination_.size() - 1);
        std::memcpy(logDestination_.data(), state.logDestination.data(), length);
        logDestination_[length] = '\0';
        loaded_ = true;
    }
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::SecuritySettings).data());
    ImGui::Separator();
    if (ImGui::Checkbox(localizer.Text(px::ui::TextId::DisconnectAutoLock).data(), &disconnectAutoLock_)) {
        port_->SetDisconnectAutoLock(disconnectAutoLock_);
    }
    ImGui::InputText(localizer.Text(px::ui::TextId::LongTermPassword).data(), password_.data(), password_.size(), ImGuiInputTextFlags_Password);
    ImGui::InputText(localizer.Text(px::ui::TextId::ConfirmPassword).data(), confirmation_.data(), confirmation_.size(),
                     ImGuiInputTextFlags_Password);
    if (ImGui::Button(localizer.Text(px::ui::TextId::SetPassword).data())) {
        passwordRejected_ = !port_->SetSecurityPassword(password_.data(), confirmation_.data());
        if (!passwordRejected_) {
            password_.fill({});
            confirmation_.fill({});
        }
    }
    if (passwordRejected_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{0.9F, 0.28F, 0.25F, 1.0F}, "%s", localizer.Text(px::ui::TextId::PasswordInvalid).data());
    }
    const auto passwordState = port_->Snapshot().passwordUpdate;
    if (passwordState == PasswordUpdateState::Updating) {
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::UpdatingPassword).data());
    } else if (passwordState == PasswordUpdateState::Updated) {
        ImGui::TextColored(ImVec4{0.18F, 0.78F, 0.36F, 1.0F}, "%s", localizer.Text(px::ui::TextId::PasswordUpdated).data());
    } else if (passwordState == PasswordUpdateState::RemoteFailed) {
        ImGui::TextColored(ImVec4{0.9F, 0.58F, 0.12F, 1.0F}, "%s", localizer.Text(px::ui::TextId::PasswordRemoteFailed).data());
    }
    ImGui::Spacing();
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::MaintenanceTools).data());
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.72F, 0.12F, 0.18F, 1.00F});
    if (ImGui::Button(localizer.Text(px::ui::TextId::ClearData).data())) {
        confirmClear_ = true;
    }
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputText(localizer.Text(px::ui::TextId::LogDestination).data(), logDestination_.data(), logDestination_.size());
    const auto logState = port_->Snapshot().logCollection;
    if (logState == LogCollectionState::Collecting) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(localizer.Text(px::ui::TextId::CollectLogs).data())) {
        port_->CollectLogs(logDestination_.data());
    }
    if (logState == LogCollectionState::Collecting) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::Collecting).data());
    } else if (logState == LogCollectionState::Completed) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{0.18F, 0.78F, 0.36F, 1.0F}, "%s", localizer.Text(px::ui::TextId::LogCollectionCompleted).data());
    } else if (logState == LogCollectionState::Failed) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{0.9F, 0.28F, 0.25F, 1.0F}, "%s", localizer.Text(px::ui::TextId::LogCollectionFailed).data());
    }
    if (confirmClear_) {
        ImGui::OpenPopup("ConfirmClearPanelData");
        confirmClear_ = false;
    }
    if (ImGui::BeginPopupModal("ConfirmClearPanelData", {}, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(localizer.Text(px::ui::TextId::ClearDataPrompt).data());
        if (ImGui::Button(localizer.Text(px::ui::TextId::Clear).data())) {
            port_->ClearData();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace px::panel::ui
