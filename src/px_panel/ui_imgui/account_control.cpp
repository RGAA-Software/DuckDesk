#include "account_control.h"

#include "px_ui/vector_icon.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <utility>

namespace px::panel::ui {

AccountControl::AccountControl(std::shared_ptr<AccountPort> port) : port_{std::move(port)} {}

void AccountControl::Draw(const px::ui::Localizer& localizer) {
    const auto account = port_->Snapshot();
    const float avatarSize{px::ui::Scale(52.0F)};
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - avatarSize) * 0.5F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, avatarSize * 0.5F);
    const bool avatarClicked{px::ui::IconOnlyButton(px::ui::VectorIcon::User, "account-avatar",
                                                    account.loggedIn ? account.username : localizer.Text(px::ui::TextId::Login),
                                                    {avatarSize, avatarSize})};
    ImGui::PopStyleVar();
    const std::string accountName{account.loggedIn ? account.username : std::string{localizer.Text(px::ui::TextId::Guest)}};
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(accountName.c_str()).x) * 0.5F);
    ImGui::TextUnformatted(accountName.c_str());
    const bool nameClicked{ImGui::IsItemClicked()};
    if (account.operation == AccountOperationState::Working) {
        const auto working = localizer.Text(px::ui::TextId::Working);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(working.data(), working.data() + working.size()).x) * 0.5F);
        ImGui::TextDisabled("%.*s", static_cast<int>(working.size()), working.data());
    } else if (account.operation == AccountOperationState::Failed) {
        ImGui::TextColored(ImVec4{0.90F, 0.22F, 0.28F, 1.0F}, "!");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", localizer.Text(px::ui::TextId::AccountOperationFailed).data());
    }
    if (avatarClicked || nameClicked) {
        if (account.loggedIn) {
            ImGui::OpenPopup("AccountMenu");
        } else {
            registerMode_ = false;
            dialogRequested_ = true;
        }
    }
    if (ImGui::BeginPopup("AccountMenu")) {
        if (ImGui::MenuItem(localizer.Text(px::ui::TextId::Logout).data())) {
            port_->Logout();
        }
        ImGui::EndPopup();
    }
    DrawDialog(localizer);
}

void AccountControl::DrawDialog(const px::ui::Localizer& localizer) {
    if (dialogRequested_) {
        ImGui::OpenPopup("AccountDialog");
        dialogRequested_ = false;
        invalidInput_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
    if (!ImGui::BeginPopupModal("AccountDialog", {}, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted(localizer.Text(registerMode_ ? px::ui::TextId::Register : px::ui::TextId::Login).data());
    ImGui::InputText(localizer.Text(px::ui::TextId::Username).data(), username_.data(), username_.size());
    ImGui::InputText(localizer.Text(px::ui::TextId::Password).data(), password_.data(), password_.size(), ImGuiInputTextFlags_Password);
    if (registerMode_) {
        ImGui::InputText(localizer.Text(px::ui::TextId::ConfirmPassword).data(), confirmation_.data(), confirmation_.size(),
                         ImGuiInputTextFlags_Password);
    }
    if (invalidInput_) {
        ImGui::TextColored(ImVec4{0.90F, 0.22F, 0.28F, 1.0F}, "%s", localizer.Text(px::ui::TextId::AccountInputInvalid).data());
    }
    if (ImGui::Button(localizer.Text(px::ui::TextId::Confirm).data())) {
        const std::string username{username_.data()};
        const std::string password{password_.data()};
        invalidInput_ = username.empty() || password.empty() || (registerMode_ && password != confirmation_.data());
        if (!invalidInput_) {
            if (registerMode_) {
                port_->Register(username, password);
            } else {
                port_->Login(username, password);
            }
            password_.fill({});
            confirmation_.fill({});
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(registerMode_ ? px::ui::TextId::Login : px::ui::TextId::Register).data())) {
        registerMode_ = !registerMode_;
        password_.fill({});
        confirmation_.fill({});
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
        password_.fill({});
        confirmation_.fill({});
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace px::panel::ui
