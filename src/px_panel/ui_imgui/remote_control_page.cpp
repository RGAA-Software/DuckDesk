#include "remote_control_page.h"

#include "px_ui/layout_metrics.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <string_view>
#include <utility>

namespace px::panel::ui {
namespace {

void LabelValue(const std::string_view label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%.*s", static_cast<int>(label.size()), label.data());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value.empty() ? "--" : value.c_str());
}

std::string EllipsizeLink(const std::string& value, const float maximumWidth) {
    if (value.empty()) {
        return "--";
    }
    if (ImGui::CalcTextSize(value.c_str()).x <= maximumWidth) {
        return value;
    }
    constexpr std::string_view suffix{"..."};
    std::size_t low{};
    std::size_t high{value.size()};
    while (low < high) {
        const std::size_t middle{low + (high - low + 1) / 2};
        const std::string candidate{value.substr(0, middle) + std::string{suffix}};
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maximumWidth) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return value.substr(0, low) + std::string{suffix};
}

void DrawLinkValue(const std::string& value) {
    const std::string display{EllipsizeLink(value, ImGui::GetContentRegionAvail().x)};
    ImGui::TextUnformatted(display.c_str());
    if (!value.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", value.c_str());
    }
}

} // namespace

RemoteControlPage::RemoteControlPage(std::shared_ptr<RemoteControlPort> port) : port_{std::move(port)} {}

void RemoteControlPage::DrawIdentity(const RemoteControlState& state, const px::ui::Localizer& localizer) {
    ImGui::SeparatorText(localizer.Text(px::ui::TextId::ThisDevice).data());
    if (ImGui::BeginTable("ThisDeviceTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.7F);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 2.3F);
        LabelValue(localizer.Text(px::ui::TextId::DeviceId), state.deviceId);
        LabelValue(localizer.Text(px::ui::TextId::DeviceName), state.deviceName);
        LabelValue(localizer.Text(px::ui::TextId::TemporaryPassword),
                   state.showTemporaryPassword ? state.temporaryPassword : std::string{"********"});
        ImGui::EndTable();
    }
    const std::string passwordButton{localizer.Text(state.showTemporaryPassword ? px::ui::TextId::Hide : px::ui::TextId::Show)};
    if (ImGui::SmallButton((passwordButton + "##password").c_str())) {
        port_->SetPasswordVisible(!state.showTemporaryPassword);
    }
    ImGui::SameLine();
    const ImVec4 statusColor{state.managerOnline ? ImVec4{0.18F, 0.78F, 0.36F, 1.0F} : ImVec4{0.90F, 0.22F, 0.28F, 1.0F}};
    ImGui::TextColored(statusColor, "%s: %s", localizer.Text(px::ui::TextId::ManagerService).data(),
                       localizer.Text(state.managerOnline ? px::ui::TextId::Online : px::ui::TextId::Offline).data());

    ImGui::SeparatorText(localizer.Text(px::ui::TextId::ConnectionInformation).data());
    if (ImGui::BeginTable("ConnectionLinks", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(180.0F));
        ImGui::TableSetupColumn("link", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(185.0F));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::DesktopLink).data());
        ImGui::TableNextColumn();
        DrawLinkValue(state.desktopLink);
        ImGui::TableNextColumn();
        const std::string copyDesktopLabel{std::string{localizer.Text(px::ui::TextId::Copy)} + "##desktop-link"};
        if (ImGui::Button(copyDesktopLabel.c_str())) {
            port_->CopyText(state.desktopLink);
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::WebClientAddress).data());
        ImGui::TableNextColumn();
        DrawLinkValue(state.webClientAddress);
        ImGui::TableNextColumn();
        const std::string copyWebLabel{std::string{localizer.Text(px::ui::TextId::Copy)} + "##web-client-address"};
        if (ImGui::Button(copyWebLabel.c_str())) {
            port_->CopyText(state.webClientAddress);
        }
        ImGui::SameLine();
        if (ImGui::Button(localizer.Text(px::ui::TextId::Open).data())) {
            port_->OpenUrl(state.webClientAddress);
        }
        ImGui::EndTable();
    }
}

void RemoteControlPage::DrawConnections(const RemoteControlState& state, const px::ui::Localizer& localizer) {
    ImGui::SeparatorText(localizer.Text(px::ui::TextId::ConnectedDevices).data());
    if (ImGui::Button(localizer.Text(px::ui::TextId::Refresh).data())) {
        port_->Refresh();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px::ui::Scale(300.0F));
    ImGui::InputTextWithHint("##remoteDevice", localizer.Text(px::ui::TextId::RemoteDeviceId).data(), &remoteDeviceId_);
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Connect).data()) && !remoteDeviceId_.empty()) {
        if (port_->RequiresPassword(remoteDeviceId_)) {
            directTarget_ = remoteDeviceId_;
            directPassword_.clear();
            directViewOnly_ = false;
            openDirectPasswordDialog_ = true;
        } else {
            port_->Connect(remoteDeviceId_, {});
        }
    }
    DrawDirectPasswordDialog(localizer);
    if (state.devices.empty()) {
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::NoRemoteDevices).data());
        return;
    }
    constexpr ImGuiTableFlags flags{ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp};
    if (ImGui::BeginTable("RemoteDevices", 4, flags)) {
        for (const auto& device : state.devices) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(device.name.empty() ? device.deviceId.c_str() : device.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(device.online ? ImVec4{0.18F, 0.78F, 0.36F, 1.0F} : ImVec4{0.90F, 0.22F, 0.28F, 1.0F}, "%s",
                               localizer.Text(device.online ? px::ui::TextId::Online : px::ui::TextId::Offline).data());
            ImGui::TableNextColumn();
            const std::string controlId{localizer.Text(px::ui::TextId::StartControl)};
            if (ImGui::SmallButton((controlId + "##" + device.streamId).c_str())) {
                if (port_->RequiresPassword(device.deviceId.empty() ? device.host : device.deviceId)) {
                    directTarget_ = device.deviceId.empty() ? device.host : device.deviceId;
                    directPassword_.clear();
                    directViewOnly_ = false;
                    openDirectPasswordDialog_ = true;
                } else {
                    port_->StartStream(device.streamId, false);
                }
            }
            ImGui::SameLine();
            const std::string viewId{localizer.Text(px::ui::TextId::ViewOnly)};
            if (ImGui::SmallButton((viewId + "##" + device.streamId).c_str())) {
                if (port_->RequiresPassword(device.deviceId.empty() ? device.host : device.deviceId)) {
                    directTarget_ = device.deviceId.empty() ? device.host : device.deviceId;
                    directPassword_.clear();
                    directViewOnly_ = true;
                    openDirectPasswordDialog_ = true;
                } else {
                    port_->StartStream(device.streamId, true);
                }
            }
            ImGui::TableNextColumn();
            const std::string stopId{localizer.Text(px::ui::TextId::StopControl)};
            if (ImGui::SmallButton((stopId + "##" + device.streamId).c_str())) {
                port_->StopStream(device.streamId);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton((std::string{localizer.Text(px::ui::TextId::More)} + "##" + device.streamId).c_str())) {
                ImGui::OpenPopup(("DeviceActions##" + device.streamId).c_str());
            }
            if (ImGui::BeginPopup(("DeviceActions##" + device.streamId).c_str())) {
                if (ImGui::MenuItem(localizer.Text(px::ui::TextId::EditDevice).data())) {
                    editingDevice_ = device;
                }
                if (ImGui::MenuItem(localizer.Text(px::ui::TextId::FileTransfer).data())) {
                    port_->StartFileTransfer(device.streamId);
                }
                ImGui::EndPopup();
            }
        }
        ImGui::EndTable();
    }
    DrawDeviceEditor(localizer);
}

void RemoteControlPage::DrawDirectPasswordDialog(const px::ui::Localizer& localizer) {
    if (openDirectPasswordDialog_) {
        ImGui::OpenPopup("DirectPassword");
        openDirectPasswordDialog_ = false;
    }
    const auto center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5F, 0.5F});
    if (!ImGui::BeginPopupModal("DirectPassword", {}, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::Password).data());
    ImGui::SetNextItemWidth(px::ui::Scale(360.0F));
    ImGui::InputText("##direct-password", &directPassword_, ImGuiInputTextFlags_Password);
    if (ImGui::Button(localizer.Text(px::ui::TextId::Connect).data()) && !directPassword_.empty()) {
        const auto target = std::move(directTarget_);
        const auto password = std::move(directPassword_);
        directTarget_.clear();
        directPassword_.clear();
        ImGui::CloseCurrentPopup();
        port_->Connect(target, password, directViewOnly_);
        directViewOnly_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
        directTarget_.clear();
        directPassword_.clear();
        directViewOnly_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RemoteControlPage::DrawDeviceEditor(const px::ui::Localizer& localizer) {
    if (editingDevice_.has_value()) {
        ImGui::OpenPopup("DeviceEditor");
    }
    if (!ImGui::BeginPopupModal("DeviceEditor", {}, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    auto& device = *editingDevice_;
    ImGui::InputText(localizer.Text(px::ui::TextId::DeviceName).data(), &device.name);
    ImGui::SeparatorText(localizer.Text(px::ui::TextId::DeviceSettings).data());
    ImGui::Checkbox(localizer.Text(px::ui::TextId::CaptureAudio).data(), &device.audio);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::EnableClipboard).data(), &device.clipboard);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::ViewOnly).data(), &device.viewOnly);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::SplitWindows).data(), &device.splitWindows);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::ForceSoftware).data(), &device.forceSoftware);
    if (ImGui::Checkbox(localizer.Text(px::ui::TextId::ForceTcp).data(), &device.forceTcp) && device.forceTcp) {
        device.forceRelay = false;
    }
    if (ImGui::Checkbox(localizer.Text(px::ui::TextId::ForceRelay).data(), &device.forceRelay) && device.forceRelay) {
        device.forceTcp = false;
    }
    ImGui::Checkbox(localizer.Text(px::ui::TextId::WaitForDebugger).data(), &device.waitForDebugger);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::ForceGdiCapture).data(), &device.forceGdiCapture);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::DisableVulkan).data(), &device.disableVulkan);
    const bool valid{!device.name.empty()};
    if (!valid) {
        ImGui::TextColored(ImVec4{0.90F, 0.22F, 0.28F, 1.0F}, "%s", localizer.Text(px::ui::TextId::InvalidDeviceSettings).data());
    }
    if (!valid) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(localizer.Text(px::ui::TextId::Save).data())) {
        port_->SaveDevice(std::move(device));
        editingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    if (!valid) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
        editingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RemoteControlPage::Draw(const px::ui::Localizer& localizer) {
    const auto state = port_->Snapshot();
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::RemoteControl).data());
    DrawIdentity(state, localizer);
    DrawConnections(state, localizer);
}

} // namespace px::panel::ui
