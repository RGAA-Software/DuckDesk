#include "remote_control_page.h"

#include "px_ui/layout_metrics.h"
#include "px_ui/vector_icon.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>
#include <utility>

namespace px::panel::ui {
namespace {

void IdentityLabel(const std::string_view label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%.*s", static_cast<int>(label.size()), label.data());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value.empty() ? "--" : value.c_str());
    ImGui::TableNextColumn();
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

std::string DeviceAddress(const RemoteDeviceCard& device) {
    if (!device.deviceId.empty()) {
        const bool isNineDigitCode{device.deviceId.size() == 9 &&
                                   std::ranges::all_of(device.deviceId, [](const unsigned char value) { return std::isdigit(value) != 0; })};
        if (isNineDigitCode) {
            return device.deviceId.substr(0, 3) + " " + device.deviceId.substr(3, 3) + " " + device.deviceId.substr(6, 3);
        }
        return device.deviceId;
    }
    return device.host.empty() ? device.streamId : device.host;
}

} // namespace

RemoteControlPage::RemoteControlPage(std::shared_ptr<RemoteControlPort> port) : port_{port}, deviceActions_{std::move(port), "recent-devices"} {}

void RemoteControlPage::DrawIdentity(const RemoteControlState& state, const px::ui::Localizer& localizer) {
    ImGui::SeparatorText(localizer.Text(px::ui::TextId::ThisDevice).data());
    if (ImGui::BeginTable("ThisDeviceTable", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.7F);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 2.3F);
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(84.0F));
        IdentityLabel(localizer.Text(px::ui::TextId::DeviceId), state.deviceId);
        if (px::ui::IconOnlyButton(px::ui::VectorIcon::Copy, "copy-device-id", localizer.Text(px::ui::TextId::Copy),
                                   {px::ui::Scale(30.0F), px::ui::Scale(26.0F)})) {
            port_->CopyText(state.deviceId);
        }
        IdentityLabel(localizer.Text(px::ui::TextId::DeviceName), state.deviceName);
        if (px::ui::IconOnlyButton(px::ui::VectorIcon::Pencil, "edit-local-device-name", localizer.Text(px::ui::TextId::EditDevice),
                                   {px::ui::Scale(30.0F), px::ui::Scale(26.0F)})) {
            localDeviceNameDraft_ = state.deviceName;
            openLocalDeviceNameDialog_ = true;
        }
        IdentityLabel(localizer.Text(px::ui::TextId::TemporaryPassword),
                      state.showTemporaryPassword ? state.temporaryPassword : std::string{"********"});
        if (px::ui::IconOnlyButton(px::ui::VectorIcon::Copy, "copy-temporary-password", localizer.Text(px::ui::TextId::Copy),
                                   {px::ui::Scale(30.0F), px::ui::Scale(26.0F)})) {
            port_->CopyText(state.temporaryPassword);
        }
        ImGui::SameLine();
        if (px::ui::IconOnlyButton(state.showTemporaryPassword ? px::ui::VectorIcon::EyeOff : px::ui::VectorIcon::Eye, "password-visibility",
                                   localizer.Text(state.showTemporaryPassword ? px::ui::TextId::Hide : px::ui::TextId::Show),
                                   {px::ui::Scale(30.0F), px::ui::Scale(26.0F)})) {
            port_->SetPasswordVisible(!state.showTemporaryPassword);
        }
        ImGui::EndTable();
    }
    if (openLocalDeviceNameDialog_) {
        ImGui::OpenPopup("EditLocalDeviceName");
        openLocalDeviceNameDialog_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
    if (ImGui::BeginPopupModal("EditLocalDeviceName", {}, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(localizer.Text(px::ui::TextId::DeviceName).data());
        ImGui::SetNextItemWidth(px::ui::Scale(360.0F));
        ImGui::InputText("##local-device-name", &localDeviceNameDraft_);
        if (ImGui::Button(localizer.Text(px::ui::TextId::Save).data()) && !localDeviceNameDraft_.empty()) {
            port_->UpdateLocalDeviceName(localDeviceNameDraft_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

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
        if (px::ui::IconButton(px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy), "copy-desktop-link")) {
            port_->CopyText(state.desktopLink);
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::WebClientAddress).data());
        ImGui::TableNextColumn();
        DrawLinkValue(state.webClientAddress);
        ImGui::TableNextColumn();
        if (px::ui::IconButton(px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy), "copy-web-client-address")) {
            port_->CopyText(state.webClientAddress);
        }
        ImGui::SameLine();
        if (px::ui::IconButton(px::ui::VectorIcon::ExternalLink, localizer.Text(px::ui::TextId::Open), "open-web-client-address")) {
            port_->OpenUrl(state.webClientAddress);
        }
        ImGui::EndTable();
    }
}

void RemoteControlPage::DrawConnections(const RemoteControlState& state, const px::ui::Localizer& localizer) {
    const std::string managerState{std::string{localizer.Text(px::ui::TextId::ConnectedDevices)} + " (" +
                                   std::string{localizer.Text(px::ui::TextId::ManagerService)} + ": " +
                                   std::string{localizer.Text(state.managerOnline ? px::ui::TextId::Online : px::ui::TextId::Offline)} + ")"};
    ImGui::SeparatorText(managerState.c_str());
    if (px::ui::IconButton(px::ui::VectorIcon::Refresh, localizer.Text(px::ui::TextId::Refresh), "refresh-devices")) {
        port_->Refresh();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px::ui::Scale(300.0F));
    ImGui::InputTextWithHint("##remoteDevice", localizer.Text(px::ui::TextId::RemoteDeviceId).data(), &remoteDeviceId_);
    ImGui::SameLine();
    if (px::ui::IconButton(px::ui::VectorIcon::Connect, localizer.Text(px::ui::TextId::Connect), "connect-device") && !remoteDeviceId_.empty()) {
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
        deviceActions_.DrawDialogs(localizer);
        return;
    }
    constexpr std::size_t maximumRecentDevices{6};
    constexpr std::size_t cardsPerRow{3};
    const std::size_t count{std::min(maximumRecentDevices, state.devices.size())};
    const float spacing{ImGui::GetStyle().ItemSpacing.x};
    const float availableWidth{ImGui::GetContentRegionAvail().x};
    const float fittingWidth{(availableWidth - spacing * static_cast<float>(cardsPerRow - 1)) / static_cast<float>(cardsPerRow)};
    const float cardWidth{std::min(px::ui::Scale(236.0F), fittingWidth)};
    for (std::size_t index{}; index < count; ++index) {
        if (index % cardsPerRow != 0) {
            ImGui::SameLine();
        }
        DrawDeviceCard(state.devices[index], localizer, index, cardWidth);
    }
    deviceActions_.DrawDialogs(localizer);
}

void RemoteControlPage::DrawDeviceCard(const RemoteDeviceCard& device, const px::ui::Localizer& localizer, const std::size_t index,
                                       const float width) {
    const ImVec2 cardSize{width, px::ui::Scale(92.0F)};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    const ImVec2 maximum{minimum.x + cardSize.x, minimum.y + cardSize.y};
    const std::string id{"recent-card-" + std::to_string(index) + "-" + device.streamId};
    ImGui::PushID(id.c_str());
    ImGui::InvisibleButton("##card", cardSize);
    auto& draw = *ImGui::GetWindowDrawList();
    const bool hovered{ImGui::IsItemHovered()};
    const ImU32 background{ImGui::GetColorU32(hovered ? ImVec4{0.18F, 0.39F, 0.79F, 1.0F} : ImVec4{0.13F, 0.30F, 0.64F, 1.0F})};
    draw.AddRectFilled(minimum, maximum, background, px::ui::Scale(9.0F));
    draw.AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border), px::ui::Scale(9.0F));
    const ImVec2 iconCenter{minimum.x + px::ui::Scale(44.0F), minimum.y + px::ui::Scale(38.0F)};
    draw.AddCircleFilled(iconCenter, px::ui::Scale(30.0F), ImGui::GetColorU32(ImVec4{0.40F, 0.58F, 0.96F, 0.28F}));
    draw.AddRect({iconCenter.x - px::ui::Scale(14.0F), iconCenter.y - px::ui::Scale(10.0F)},
                 {iconCenter.x + px::ui::Scale(14.0F), iconCenter.y + px::ui::Scale(8.0F)}, ImGui::GetColorU32(ImVec4{0.64F, 0.75F, 1.0F, 0.45F}),
                 px::ui::Scale(2.0F), 0, px::ui::Scale(2.0F));
    const ImU32 statusColor{ImGui::GetColorU32(device.online ? ImVec4{0.16F, 0.90F, 0.40F, 1.0F} : ImVec4{0.96F, 0.30F, 0.32F, 1.0F})};
    draw.AddCircleFilled({maximum.x - px::ui::Scale(14.0F), minimum.y + px::ui::Scale(14.0F)}, px::ui::Scale(4.0F), statusColor);
    const std::string address{DeviceAddress(device)};
    const std::string name{device.name.empty() ? address : device.name};
    draw.AddText({minimum.x + px::ui::Scale(16.0F), maximum.y - px::ui::Scale(45.0F)}, ImGui::GetColorU32(ImGuiCol_Text), address.c_str());
    draw.AddText({minimum.x + px::ui::Scale(16.0F), maximum.y - px::ui::Scale(23.0F)}, ImGui::GetColorU32(ImGuiCol_TextDisabled), name.c_str());
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        deviceActions_.Start(device, false);
    }
    if (ImGui::BeginPopupContextItem("DeviceActions")) {
        deviceActions_.DrawContextMenu(device, localizer);
        ImGui::EndPopup();
    }
    ImGui::PopID();
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

void RemoteControlPage::Draw(const px::ui::Localizer& localizer) {
    const auto state = port_->Snapshot();
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::RemoteControl).data());
    DrawIdentity(state, localizer);
    DrawConnections(state, localizer);
}

} // namespace px::panel::ui
