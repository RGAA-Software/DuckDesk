#include "account_control.h"

#include <imgui.h>

#include <utility>

namespace px::panel::ui {

AccountControl::AccountControl(std::shared_ptr<AccountPort> port) : port_{std::move(port)} {}

void AccountControl::Draw(const px::ui::Localizer& localizer) {
    const auto account = port_->Snapshot();
    ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::Account).data());
    ImGui::TextUnformatted(account.loggedIn ? account.username.c_str() : localizer.Text(px::ui::TextId::Guest).data());
    if (account.operation == AccountOperationState::Working) {
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::Working).data());
    } else if (account.operation == AccountOperationState::Failed) {
        ImGui::TextColored(ImVec4{0.90F, 0.22F, 0.28F, 1.0F}, "%s", localizer.Text(px::ui::TextId::AccountOperationFailed).data());
    }
    if (account.loggedIn) {
        if (ImGui::SmallButton(localizer.Text(px::ui::TextId::Logout).data())) {
            port_->Logout();
        }
    } else if (ImGui::SmallButton(localizer.Text(px::ui::TextId::Login).data())) {
        registerMode_ = false;
        dialogRequested_ = true;
    }
    if (!account.loggedIn) {
        ImGui::SameLine();
        if (ImGui::SmallButton(localizer.Text(px::ui::TextId::Register).data())) {
            registerMode_ = true;
            dialogRequested_ = true;
        }
    }
    DrawDialog(localizer);
}

void AccountControl::DrawDialog(const px::ui::Localizer& localizer) {
    if (dialogRequested_) {
        ImGui::OpenPopup("AccountDialog");
        dialogRequested_ = false;
        invalidInput_ = false;
    }
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
    if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
        password_.fill({});
        confirmation_.fill({});
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace px::panel::ui
