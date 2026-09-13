#include "account_control.h"

#include "px_ui/components/button.h"
#include "px_ui/components/form.h"
#include "px_ui/components/identity.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <utility>

namespace px::panel::ui {

AccountControl::AccountControl(std::shared_ptr<AccountPort> port) : port_{std::move(port)} {}

void AccountControl::Draw(const px::ui::Localizer& localizer) {
    const auto account = port_->Snapshot();
    const float avatarSize{px::ui::Scale(48.0F)};
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - avatarSize) * 0.5F);
    const bool avatarClicked{px::ui::AvatarButton({"account-avatar"}, account.loggedIn ? account.username : localizer.Text(px::ui::TextId::Login),
                                                  avatarSize, account.loggedIn)};
    const std::string accountName{account.loggedIn ? account.username : std::string{localizer.Text(px::ui::TextId::Guest)}};
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(accountName.c_str()).x) * 0.5F);
    ImGui::TextUnformatted(accountName.c_str());
    const bool nameClicked{ImGui::IsItemClicked()};
    if (account.operation == AccountOperationState::Working) {
        const auto working = localizer.Text(px::ui::TextId::Working);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(working.data(), working.data() + working.size()).x) * 0.5F);
        px::ui::MutedText(working);
    } else if (account.operation == AccountOperationState::Failed) {
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("!").x) * 0.5F);
        px::ui::StatusBadge("!", px::ui::BadgeVariant::Destructive);
        px::ui::Tooltip(localizer.Text(px::ui::TextId::AccountOperationFailed));
    }
    if (avatarClicked || nameClicked) {
        if (account.loggedIn) {
            px::ui::OpenPopup({"AccountMenu"});
        } else {
            registerMode_ = false;
            dialogRequested_ = true;
        }
    }
    {
        px::ui::PopupScope menu{{"AccountMenu"}};
        if (menu.Open()) {
            if (px::ui::MenuAction({"account-logout"}, localizer.Text(px::ui::TextId::Logout),
                                   {.icon = px::ui::VectorIcon::LogOut, .variant = px::ui::MenuItemVariant::Destructive})) {
                port_->Logout();
            }
        }
    }
    DrawDialog(localizer);
}

void AccountControl::DrawDialog(const px::ui::Localizer& localizer) {
    if (dialogRequested_) {
        px::ui::OpenModal({"AccountDialog"});
        dialogRequested_ = false;
        invalidInput_ = false;
    }
    px::ui::ModalScope dialog{{"AccountDialog"}, 440.0F};
    if (!dialog.Open()) {
        return;
    }
    static_cast<void>(px::ui::DialogHeader({"close-account-dialog"}, localizer.Text(registerMode_ ? px::ui::TextId::Register : px::ui::TextId::Login),
                                           {}, {.icon = px::ui::VectorIcon::User, .closeable = false}));
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::Username));
    static_cast<void>(px::ui::TextField({"account-username"}, username_));
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::Password));
    static_cast<void>(px::ui::PasswordField({"account-password"}, password_));
    if (registerMode_) {
        px::ui::FieldLabel(localizer.Text(px::ui::TextId::ConfirmPassword));
        static_cast<void>(px::ui::PasswordField({"account-confirm-password"}, confirmation_));
    }
    if (invalidInput_) {
        px::ui::FieldError(localizer.Text(px::ui::TextId::AccountInputInvalid));
    }
    const float secondaryWidth{px::ui::Scale(88.0F)};
    const float primaryWidth{px::ui::Scale(96.0F)};
    px::ui::DialogFooter(secondaryWidth * 2.0F + primaryWidth + ImGui::GetStyle().ItemSpacing.x * 2.0F);
    if (px::ui::ActionButton({"account-mode"}, localizer.Text(registerMode_ ? px::ui::TextId::Login : px::ui::TextId::Register),
                             {.variant = px::ui::ButtonVariant::Ghost, .width = secondaryWidth})) {
        registerMode_ = !registerMode_;
        password_.clear();
        confirmation_.clear();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"account-cancel"}, localizer.Text(px::ui::TextId::Cancel),
                             {.variant = px::ui::ButtonVariant::Outline, .width = secondaryWidth})) {
        password_.clear();
        confirmation_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"account-confirm"}, localizer.Text(px::ui::TextId::Confirm), {.width = primaryWidth})) {
        invalidInput_ = username_.empty() || password_.empty() || (registerMode_ && password_ != confirmation_);
        if (!invalidInput_) {
            if (registerMode_) {
                port_->Register(username_, password_);
            } else {
                port_->Login(username_, password_);
            }
            password_.clear();
            confirmation_.clear();
            ImGui::CloseCurrentPopup();
        }
    }
}

} // namespace px::panel::ui
