#include "cloud_applications_page.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <string>
#include <utility>

namespace px::panel::ui {

CloudApplicationsPage::CloudApplicationsPage(std::shared_ptr<CloudApplicationsPort> port) : port_{std::move(port)} {}

void CloudApplicationsPage::Draw(const px::ui::Localizer& localizer) {
    if (const auto request = port_->PendingPasswordRequest(); request && passwordStreamId_.empty()) {
        passwordStreamId_ = request->streamId;
        password_.clear();
        passwordDialogOpen_ = true;
    }
    DrawPasswordDialog(localizer);
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::CloudApplications).data());
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Refresh).data())) {
        port_->Refresh();
    }
    ImGui::Separator();
    const auto applications = port_->Snapshot();
    if (applications.empty()) {
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::NoCloudApplications).data());
        return;
    }
    constexpr ImGuiTableFlags flags{ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp};
    if (!ImGui::BeginTable("CloudApplications", 5, flags)) {
        return;
    }
    for (const auto& application : applications) {
        const bool running{application.instanceState == "running"};
        const bool busy{application.instanceState == "starting" || application.instanceState == "stopping"};
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(application.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(running ? ImVec4{0.18F, 0.78F, 0.36F, 1.0F} : ImVec4{0.48F, 0.53F, 0.62F, 1.0F}, "%s",
                           localizer.Text(running ? px::ui::TextId::Running : px::ui::TextId::Stopped).data());
        ImGui::TableNextColumn();
        const std::string startText{localizer.Text(running ? px::ui::TextId::EnterApplication : px::ui::TextId::StartApplication)};
        if (busy) {
            ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton((startText + "##" + application.streamId).c_str())) {
            port_->Start(application.streamId, false);
        }
        ImGui::SameLine();
        const std::string viewText{localizer.Text(px::ui::TextId::ViewOnly)};
        if (ImGui::SmallButton((viewText + "##" + application.streamId).c_str())) {
            port_->Start(application.streamId, true);
        }
        if (busy) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        const std::string stopText{localizer.Text(px::ui::TextId::StopApplication)};
        if (ImGui::SmallButton((stopText + "##" + application.streamId).c_str())) {
            port_->Stop(application.streamId);
        }
        ImGui::TableNextColumn();
        bool forceTcp{application.forceTcp};
        if (application.rdpMode) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Checkbox((std::string{localizer.Text(px::ui::TextId::ForceTcp)} + "##" + application.streamId).c_str(), &forceTcp)) {
            port_->SetForceTcp(application.streamId, forceTcp);
        }
        ImGui::TableNextColumn();
        bool forceRelay{application.forceRelay};
        if (ImGui::Checkbox((std::string{localizer.Text(px::ui::TextId::ForceRelay)} + "##" + application.streamId).c_str(), &forceRelay)) {
            port_->SetForceRelay(application.streamId, forceRelay);
        }
        if (application.rdpMode) {
            ImGui::EndDisabled();
        }
    }
    ImGui::EndTable();
}

void CloudApplicationsPage::DrawPasswordDialog(const px::ui::Localizer& localizer) {
    if (passwordDialogOpen_) {
        ImGui::OpenPopup("CloudApplicationPassword");
        passwordDialogOpen_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
    if (!ImGui::BeginPopupModal("CloudApplicationPassword", {}, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::Password).data());
    ImGui::SetNextItemWidth(360.0F);
    ImGui::InputText("##cloud-password", &password_, ImGuiInputTextFlags_Password);
    if (ImGui::Button(localizer.Text(px::ui::TextId::Connect).data()) && !password_.empty()) {
        port_->SubmitPassword(passwordStreamId_, std::move(password_));
        passwordStreamId_.clear();
        password_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
        port_->CancelPassword(passwordStreamId_);
        passwordStreamId_.clear();
        password_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace px::panel::ui
