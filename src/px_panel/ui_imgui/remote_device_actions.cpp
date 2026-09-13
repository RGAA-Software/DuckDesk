#include "remote_device_actions.h"

#include "px_ui/components/button.h"
#include "px_ui/components/form.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <utility>

namespace px::panel::ui {

RemoteDeviceActions::RemoteDeviceActions(std::shared_ptr<RemoteControlPort> port, std::string idScope)
    : port_{std::move(port)}, idScope_{std::move(idScope)} {}

std::string RemoteDeviceActions::PopupId(const std::string_view name) const {
    return std::string{name} + "##" + idScope_;
}

void RemoteDeviceActions::Start(const RemoteDeviceCard& device, const bool viewOnly) {
    const std::string target{device.deviceId.empty() ? device.host : device.deviceId};
    if (port_->RequiresPassword(target)) {
        pendingTarget_ = target;
        pendingPassword_.clear();
        pendingViewOnly_ = viewOnly;
        openPasswordDialog_ = true;
        return;
    }
    port_->StartStream(device.streamId, viewOnly);
}

void RemoteDeviceActions::Edit(const RemoteDeviceCard& device) {
    editingDevice_ = device;
    openEditor_ = true;
}

void RemoteDeviceActions::FileTransfer(const RemoteDeviceCard& device) {
    port_->StartFileTransfer(device.streamId);
}

void RemoteDeviceActions::Command(const RemoteDeviceCard& device, const RemoteDeviceCommand command) {
    commandingDevice_ = device;
    pendingCommand_ = command;
    openCommandConfirmation_ = true;
}

void RemoteDeviceActions::Remove(const RemoteDeviceCard& device) {
    removingDevice_ = device;
    openRemoveConfirmation_ = true;
}

void RemoteDeviceActions::DrawContextMenu(const RemoteDeviceCard& device, const px::ui::Localizer& localizer) {
    if (px::ui::MenuAction({"device-start"}, localizer.Text(px::ui::TextId::StartControl), {.icon = px::ui::VectorIcon::Play}))
        Start(device, false);
    if (px::ui::MenuAction({"device-view"}, localizer.Text(px::ui::TextId::ViewOnly), {.icon = px::ui::VectorIcon::Eye}))
        Start(device, true);
    px::ui::MenuSeparator();
    if (px::ui::MenuAction({"device-edit"}, localizer.Text(px::ui::TextId::EditDevice), {.icon = px::ui::VectorIcon::Pencil}))
        Edit(device);
    if (px::ui::MenuAction({"device-files"}, localizer.Text(px::ui::TextId::FileTransfer), {.icon = px::ui::VectorIcon::FileTransfer}))
        FileTransfer(device);
    px::ui::MenuSeparator();
    if (px::ui::MenuAction({"device-delete"}, localizer.Text(px::ui::TextId::Delete),
                           {.icon = px::ui::VectorIcon::Trash, .variant = px::ui::MenuItemVariant::Destructive}))
        Remove(device);
}

void RemoteDeviceActions::DrawPasswordDialog(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoteDevicePassword")};
    if (openPasswordDialog_) {
        px::ui::OpenModal({popupId});
        openPasswordDialog_ = false;
    }
    px::ui::ModalScope dialog{{popupId}, 420.0F};
    if (!dialog.Open())
        return;
    static_cast<void>(px::ui::DialogHeader({"close-remote-device-password"}, localizer.Text(px::ui::TextId::Password), {},
                                           {.icon = px::ui::VectorIcon::Shield, .closeable = false}));
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::Password));
    static_cast<void>(px::ui::PasswordField({"remote-device-password"}, pendingPassword_));
    const float passwordButtonWidth{px::ui::Scale(96.0F)};
    px::ui::DialogFooter(passwordButtonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
    if (px::ui::ActionButton({"remote-device-cancel"}, localizer.Text(px::ui::TextId::Cancel),
                             {.variant = px::ui::ButtonVariant::Outline, .width = passwordButtonWidth})) {
        pendingTarget_.clear();
        pendingPassword_.clear();
        pendingViewOnly_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"remote-device-connect"}, localizer.Text(px::ui::TextId::Connect),
                             {.icon = px::ui::VectorIcon::Connect, .width = passwordButtonWidth, .disabled = pendingPassword_.empty()})) {
        port_->Connect(std::move(pendingTarget_), std::move(pendingPassword_), pendingViewOnly_);
        pendingViewOnly_ = false;
        ImGui::CloseCurrentPopup();
    }
}

void RemoteDeviceActions::DrawEditor(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoteDeviceEditor")};
    if (openEditor_) {
        px::ui::OpenModal({popupId});
        openEditor_ = false;
    }
    px::ui::ModalScope dialog{{popupId}, 360.0F};
    if (!dialog.Open())
        return;
    if (!editingDevice_) {
        ImGui::CloseCurrentPopup();
        return;
    }
    auto& device = *editingDevice_;
    static_cast<void>(px::ui::DialogHeader({"close-device-editor"}, localizer.Text(px::ui::TextId::EditDevice), {},
                                           {.icon = px::ui::VectorIcon::Settings, .closeable = false}));
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::DeviceName));
    static_cast<void>(px::ui::TextField({"remote-device-name"}, device.name));
    px::ui::HorizontalSeparator();
    static_cast<void>(px::ui::CheckboxField({"device-audio"}, localizer.Text(px::ui::TextId::CaptureAudio), device.audio));
    static_cast<void>(px::ui::CheckboxField({"device-clipboard"}, localizer.Text(px::ui::TextId::EnableClipboard), device.clipboard));
    static_cast<void>(px::ui::CheckboxField({"device-view-only"}, localizer.Text(px::ui::TextId::ViewOnly), device.viewOnly));
    static_cast<void>(px::ui::CheckboxField({"device-software"}, localizer.Text(px::ui::TextId::ForceSoftware), device.forceSoftware));
    if (px::ui::CheckboxField({"device-tcp"}, localizer.Text(px::ui::TextId::ForceTcp), device.forceTcp) && device.forceTcp)
        device.forceRelay = false;
    if (px::ui::CheckboxField({"device-relay"}, localizer.Text(px::ui::TextId::ForceRelay), device.forceRelay) && device.forceRelay)
        device.forceTcp = false;
    const bool valid{!device.name.empty()};
    if (!valid)
        px::ui::FieldError(localizer.Text(px::ui::TextId::InvalidDeviceSettings));
    const float editorButtonWidth{px::ui::Scale(96.0F)};
    px::ui::DialogFooter(editorButtonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
    if (px::ui::ActionButton({"device-edit-cancel"}, localizer.Text(px::ui::TextId::Cancel),
                             {.variant = px::ui::ButtonVariant::Outline, .width = editorButtonWidth})) {
        editingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"device-save"}, localizer.Text(px::ui::TextId::Save), {.width = editorButtonWidth, .disabled = !valid})) {
        port_->SaveDevice(std::move(device));
        editingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
}

void RemoteDeviceActions::DrawCommandConfirmation(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoteDeviceCommand")};
    if (openCommandConfirmation_) {
        px::ui::OpenModal({popupId});
        openCommandConfirmation_ = false;
    }
    px::ui::ModalScope dialog{{popupId}, 420.0F};
    if (!dialog.Open())
        return;
    if (!commandingDevice_ || !pendingCommand_) {
        ImGui::CloseCurrentPopup();
        return;
    }

    px::ui::TextId commandTextId{px::ui::TextId::LockDevice};
    px::ui::VectorIcon commandIcon{px::ui::VectorIcon::Shield};
    px::ui::ButtonVariant commandVariant{px::ui::ButtonVariant::Primary};
    switch (*pendingCommand_) {
    case RemoteDeviceCommand::Lock:
        break;
    case RemoteDeviceCommand::Restart:
        commandTextId = px::ui::TextId::RestartDevice;
        commandIcon = px::ui::VectorIcon::Restart;
        commandVariant = px::ui::ButtonVariant::Destructive;
        break;
    case RemoteDeviceCommand::Shutdown:
        commandTextId = px::ui::TextId::ShutdownDevice;
        commandIcon = px::ui::VectorIcon::Power;
        commandVariant = px::ui::ButtonVariant::Destructive;
        break;
    }
    static_cast<void>(px::ui::DialogHeader({"close-device-command"}, localizer.Text(commandTextId),
                                           localizer.Text(px::ui::TextId::ConfirmDeviceAction),
                                           {.icon = px::ui::VectorIcon::TriangleAlert, .tone = px::ui::BadgeVariant::Warning, .closeable = false}));
    const float commandButtonWidth{px::ui::Scale(96.0F)};
    px::ui::DialogFooter(commandButtonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
    if (px::ui::ActionButton({"device-command-cancel"}, localizer.Text(px::ui::TextId::Cancel),
                             {.variant = px::ui::ButtonVariant::Outline, .width = commandButtonWidth})) {
        commandingDevice_.reset();
        pendingCommand_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"device-command-confirm"}, localizer.Text(commandTextId),
                             {.variant = commandVariant, .icon = commandIcon, .width = commandButtonWidth})) {
        port_->SendDeviceCommand(commandingDevice_->streamId, *pendingCommand_);
        commandingDevice_.reset();
        pendingCommand_.reset();
        ImGui::CloseCurrentPopup();
    }
}

void RemoteDeviceActions::DrawRemoveConfirmation(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoveRemoteDevice")};
    if (openRemoveConfirmation_) {
        px::ui::OpenModal({popupId});
        openRemoveConfirmation_ = false;
    }
    px::ui::ModalScope dialog{{popupId}, 420.0F};
    if (!dialog.Open())
        return;
    static_cast<void>(
        px::ui::DialogHeader({"close-device-remove"}, localizer.Text(px::ui::TextId::Delete), localizer.Text(px::ui::TextId::RemoveDevicePrompt),
                             {.icon = px::ui::VectorIcon::TriangleAlert, .tone = px::ui::BadgeVariant::Destructive, .closeable = false}));
    const float removeButtonWidth{px::ui::Scale(96.0F)};
    px::ui::DialogFooter(removeButtonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
    if (px::ui::ActionButton({"device-remove-cancel"}, localizer.Text(px::ui::TextId::Cancel),
                             {.variant = px::ui::ButtonVariant::Outline, .width = removeButtonWidth})) {
        removingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"device-remove"}, localizer.Text(px::ui::TextId::Delete),
                             {.variant = px::ui::ButtonVariant::Destructive, .width = removeButtonWidth}) &&
        removingDevice_) {
        port_->DeleteDevice(removingDevice_->streamId);
        removingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
}

void RemoteDeviceActions::DrawDialogs(const px::ui::Localizer& localizer) {
    DrawPasswordDialog(localizer);
    DrawEditor(localizer);
    DrawCommandConfirmation(localizer);
    DrawRemoveConfirmation(localizer);
}

} // namespace px::panel::ui
