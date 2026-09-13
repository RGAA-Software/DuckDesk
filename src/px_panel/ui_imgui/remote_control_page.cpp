#include "remote_control_page.h"
#include "panel_layout.h"

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>
#include <utility>

namespace px::panel::ui {
namespace {

constexpr ImGuiWindowFlags fixedCardFlags{ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};
constexpr float linkTextScale{15.0F / 16.0F};

void IdentityLabel(const std::string_view label, const std::string& value, const bool strong = false) {
    ImGui::TableNextRow(ImGuiTableRowFlags_None, px::ui::Scale(40.0F));
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%.*s", static_cast<int>(label.size()), label.data());
    ImGui::TableNextColumn();
    if (strong) {
        px::ui::StrongText(value.empty() ? "--" : value);
    } else {
        ImGui::TextUnformatted(value.empty() ? "--" : value.c_str());
    }
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

std::string FormatDeviceId(const std::string& value) {
    const bool isNineDigitCode{value.size() == 9 &&
                               std::ranges::all_of(value, [](const unsigned char character) { return std::isdigit(character) != 0; })};
    if (isNineDigitCode) {
        return value.substr(0, 3) + " " + value.substr(3, 3) + " " + value.substr(6, 3);
    }
    return value;
}

std::string DeviceAddress(const RemoteDeviceCard& device) {
    if (!device.deviceId.empty()) {
        return FormatDeviceId(device.deviceId);
    }
    return device.host.empty() ? device.streamId : device.host;
}

} // namespace

RemoteControlPage::RemoteControlPage(std::shared_ptr<RemoteControlPort> port) : port_{port}, deviceActions_{std::move(port), "recent-devices"} {}

void RemoteControlPage::DrawIdentity(const RemoteControlState& state, const px::ui::Localizer& localizer) {
    const float gap{layout::CardGap()};
    const float availableWidth{ImGui::GetContentRegionAvail().x};
    const float cardWidth{(availableWidth - gap) * 0.46F};
    const float cardHeight{px::ui::Scale(185.0F)};
    {
        px::ui::CardScope identity{{"local-identity"}, {cardWidth, cardHeight}, fixedCardFlags};
        if (identity.Visible()) {
            px::ui::SectionTitle(localizer.Text(px::ui::TextId::ConnectionCredentials));
            px::ui::HorizontalSeparator();
        }
        if (identity.Visible() && ImGui::BeginTable("ThisDeviceTable", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(88.0F));
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(92.0F));
            const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
            ImGui::PushStyleColor(ImGuiCol_Text, tokens.primary);
            IdentityLabel(localizer.Text(px::ui::TextId::DeviceId), FormatDeviceId(state.deviceId), true);
            ImGui::PopStyleColor();
            if (px::ui::IconAction({"copy-device-id"}, px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy),
                                   {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::IconXs})) {
                port_->CopyText(state.deviceId);
            }
            IdentityLabel(localizer.Text(px::ui::TextId::Password), state.showTemporaryPassword ? state.temporaryPassword : std::string{"********"});
            if (px::ui::IconAction({"copy-temporary-password"}, px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy),
                                   {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::IconXs})) {
                port_->CopyText(state.temporaryPassword);
            }
            ImGui::SameLine();
            if (px::ui::IconAction({"password-visibility"}, state.showTemporaryPassword ? px::ui::VectorIcon::EyeOff : px::ui::VectorIcon::Eye,
                                   localizer.Text(state.showTemporaryPassword ? px::ui::TextId::Hide : px::ui::TextId::Show),
                                   {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::IconXs})) {
                port_->SetPasswordVisible(!state.showTemporaryPassword);
            }
            ImGui::SameLine();
            if (px::ui::IconAction({"refresh-temporary-password"}, px::ui::VectorIcon::Refresh, localizer.Text(px::ui::TextId::Refresh),
                                   {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::IconXs})) {
                port_->RefreshTemporaryPassword();
            }
            IdentityLabel(localizer.Text(px::ui::TextId::DeviceName), state.deviceName);
            if (px::ui::IconAction({"edit-local-device-name"}, px::ui::VectorIcon::Pencil, localizer.Text(px::ui::TextId::EditDevice),
                                   {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::IconXs})) {
                localDeviceNameDraft_ = state.deviceName;
                openLocalDeviceNameDialog_ = true;
            }
            ImGui::EndTable();
        }
    }

    ImGui::SameLine(0.0F, gap);
    {
        px::ui::CardScope links{{"connection-links"}, {0.0F, cardHeight}, fixedCardFlags};
        if (links.Visible()) {
            px::ui::SectionTitle(localizer.Text(px::ui::TextId::ShareAndWebAccess));
            px::ui::HorizontalSeparator();
        }
        if (links.Visible()) {
            px::ui::FieldLabel(localizer.Text(px::ui::TextId::DesktopLink));
            const float iconActionWidth{px::ui::Scale(34.0F)};
            const float actionsWidth{iconActionWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x};
            const float desktopWidth{ImGui::GetContentRegionAvail().x - actionsWidth - ImGui::GetStyle().ItemSpacing.x};
            ImGui::SetWindowFontScale(linkTextScale);
            std::string desktopDisplay{EllipsizeLink(state.desktopLink, desktopWidth - px::ui::Scale(24.0F))};
            static_cast<void>(px::ui::TextField({"desktop-link-display"}, desktopDisplay, {}, {.width = desktopWidth, .readOnly = true}));
            ImGui::SetWindowFontScale(1.0F);
            px::ui::Tooltip(state.desktopLink);
            ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x);
            if (px::ui::IconAction({"copy-desktop-link"}, px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy),
                                   {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::IconSm})) {
                port_->CopyText(state.desktopLink);
            }
            ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x);
            if (px::ui::IconAction(
                    {"qr-desktop-link"}, px::ui::VectorIcon::QrCode, localizer.Text(px::ui::TextId::QrCode),
                    {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::IconSm, .disabled = state.desktopLink.empty()})) {
                qrDialog_.Open(state.desktopLink, ConnectionQrKind::DesktopLink);
            }
            px::ui::FieldLabel(localizer.Text(px::ui::TextId::WebClientAddress));
            const float webWidth{ImGui::GetContentRegionAvail().x - actionsWidth - ImGui::GetStyle().ItemSpacing.x};
            ImGui::SetWindowFontScale(linkTextScale);
            std::string webDisplay{EllipsizeLink(state.webClientAddress, webWidth - px::ui::Scale(24.0F))};
            static_cast<void>(px::ui::TextField({"web-link-display"}, webDisplay, {}, {.width = webWidth, .readOnly = true}));
            ImGui::SetWindowFontScale(1.0F);
            px::ui::Tooltip(state.webClientAddress);
            ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x);
            if (px::ui::IconAction({"copy-web-client-address"}, px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy),
                                   {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::IconSm})) {
                port_->CopyText(state.webClientAddress);
            }
            ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x);
            if (px::ui::IconAction(
                    {"qr-web-client-address"}, px::ui::VectorIcon::QrCode, localizer.Text(px::ui::TextId::QrCode),
                    {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::IconSm, .disabled = state.webClientAddress.empty()})) {
                qrDialog_.Open(state.webClientAddress, ConnectionQrKind::WebClientAddress);
            }
        }
    }
    if (openLocalDeviceNameDialog_) {
        px::ui::OpenModal({"EditLocalDeviceName"});
        openLocalDeviceNameDialog_ = false;
    }
    {
        px::ui::ModalScope dialog{{"EditLocalDeviceName"}, 420.0F};
        if (dialog.Open()) {
            static_cast<void>(px::ui::DialogHeader({"close-local-device-name"}, localizer.Text(px::ui::TextId::DeviceName), {},
                                                   {.icon = px::ui::VectorIcon::Pencil, .closeable = false}));
            static_cast<void>(px::ui::TextField({"local-device-name"}, localDeviceNameDraft_));
            const float buttonWidth{px::ui::Scale(96.0F)};
            px::ui::DialogFooter(buttonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
            if (px::ui::ActionButton({"cancel-local-name"}, localizer.Text(px::ui::TextId::Cancel),
                                     {.variant = px::ui::ButtonVariant::Outline, .width = buttonWidth})) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (px::ui::ActionButton({"save-local-name"}, localizer.Text(px::ui::TextId::Save),
                                     {.width = buttonWidth, .disabled = localDeviceNameDraft_.empty()})) {
                port_->UpdateLocalDeviceName(localDeviceNameDraft_);
                ImGui::CloseCurrentPopup();
            }
        }
    }
}

void RemoteControlPage::DrawConnections(const RemoteControlState& state, const px::ui::Localizer& localizer,
                                        const px::desktop::PlatformIconAtlas& platformIcons) {
    {
        px::ui::CardScope connection{{"connection-workflow"}, {0.0F, px::ui::Scale(94.0F)}, fixedCardFlags};
        if (connection.Visible()) {
            px::ui::SectionTitle(localizer.Text(px::ui::TextId::ConnectToRemoteDevice));
            const float actionWidth{px::ui::Scale(78.0F)};
            const float fittingFieldWidth{ImGui::GetContentRegionAvail().x - actionWidth - ImGui::GetStyle().ItemSpacing.x};
            static_cast<void>(px::ui::TextField({"remote-device"}, remoteDeviceId_, localizer.Text(px::ui::TextId::RemoteDeviceId),
                                                {.width = fittingFieldWidth, .leadingIcon = px::ui::VectorIcon::Connect}));
            ImGui::SameLine();
            if (px::ui::ActionButton({"connect-device"}, localizer.Text(px::ui::TextId::Connect),
                                     {.size = px::ui::WidgetSize::Sm,
                                      .icon = px::ui::VectorIcon::Connect,
                                      .width = actionWidth,
                                      .disabled = remoteDeviceId_.empty()})) {
                if (port_->RequiresPassword(remoteDeviceId_)) {
                    directTarget_ = remoteDeviceId_;
                    directPassword_.clear();
                    directViewOnly_ = false;
                    openDirectPasswordDialog_ = true;
                } else {
                    port_->Connect(remoteDeviceId_, {});
                }
            }
        }
    }
    DrawDirectPasswordDialog(localizer);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px::ui::Scale(13.0F));
    px::ui::PageTitle(localizer.Text(px::ui::TextId::RecentDevices));
    ImGui::SameLine(0.0F, px::ui::Scale(14.0F));
    if (px::ui::ActionButton(
            {"refresh-devices"}, localizer.Text(px::ui::TextId::Refresh),
            {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Xs, .icon = px::ui::VectorIcon::Refresh, .circular = true})) {
        port_->Refresh();
    }
    if (state.devices.empty()) {
        px::ui::EmptyState(px::ui::VectorIcon::Monitor, localizer.Text(px::ui::TextId::NoRemoteDevices), {});
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
        DrawDeviceCard(state.devices[index], localizer, platformIcons, index, cardWidth);
    }
    deviceActions_.DrawDialogs(localizer);
}

void RemoteControlPage::DrawDeviceCard(const RemoteDeviceCard& device, const px::ui::Localizer& localizer,
                                       const px::desktop::PlatformIconAtlas& platformIcons, const std::size_t index, const float width) {
    const ImVec2 cardSize{width, layout::CompactCardHeight()};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    const ImVec2 maximum{minimum.x + cardSize.x, minimum.y + cardSize.y};
    const std::string id{"recent-card-" + std::to_string(index) + "-" + device.streamId};
    ImGui::PushID(id.c_str());
    ImGui::InvisibleButton("##card", cardSize);
    auto& draw = *ImGui::GetWindowDrawList();
    const bool hovered{ImGui::IsItemHovered()};
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    const ImU32 background{ImGui::GetColorU32(hovered ? tokens.accent : tokens.card)};
    draw.AddRectFilled(minimum, maximum, background, px::ui::Scale(9.0F));
    draw.AddRect(minimum, maximum, ImGui::GetColorU32(hovered ? tokens.ring : tokens.border), px::ui::Scale(9.0F));
    const float iconSize{px::ui::Scale(32.0F)};
    platformIcons.Draw(device.platform, {minimum.x + px::ui::Scale(14.0F), minimum.y + px::ui::Scale(14.0F)}, iconSize,
                       ImGui::GetColorU32(tokens.primary));
    const ImU32 statusColor{ImGui::GetColorU32(device.online ? tokens.success : tokens.mutedForeground)};
    draw.AddCircleFilled({maximum.x - px::ui::Scale(14.0F), minimum.y + px::ui::Scale(14.0F)}, px::ui::Scale(4.0F), statusColor);
    const std::string address{DeviceAddress(device)};
    const std::string name{device.name.empty() ? address : device.name};
    draw.AddText({minimum.x + px::ui::Scale(58.0F), minimum.y + px::ui::Scale(8.0F)}, ImGui::GetColorU32(ImGuiCol_Text), address.c_str());
    draw.AddText({minimum.x + px::ui::Scale(58.0F), minimum.y + px::ui::Scale(31.0F)}, ImGui::GetColorU32(ImGuiCol_TextDisabled), name.c_str());
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        deviceActions_.Start(device, false);
    }
    {
        px::ui::ContextMenuScope menu{{"DeviceActions"}};
        if (menu.Open()) {
            deviceActions_.DrawContextMenu(device, localizer);
        }
    }
    ImGui::PopID();
}

void RemoteControlPage::DrawDirectPasswordDialog(const px::ui::Localizer& localizer) {
    if (openDirectPasswordDialog_) {
        px::ui::OpenModal({"DirectPassword"});
        openDirectPasswordDialog_ = false;
    }
    px::ui::ModalScope dialog{{"DirectPassword"}, 420.0F};
    if (!dialog.Open()) {
        return;
    }
    static_cast<void>(px::ui::DialogHeader({"close-direct-password"}, localizer.Text(px::ui::TextId::Password), {},
                                           {.icon = px::ui::VectorIcon::Shield, .closeable = false}));
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::Password));
    static_cast<void>(px::ui::PasswordField({"direct-password"}, directPassword_));
    const float buttonWidth{px::ui::Scale(96.0F)};
    px::ui::DialogFooter(buttonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
    if (px::ui::ActionButton({"direct-cancel"}, localizer.Text(px::ui::TextId::Cancel),
                             {.variant = px::ui::ButtonVariant::Outline, .width = buttonWidth})) {
        directTarget_.clear();
        directPassword_.clear();
        directViewOnly_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"direct-connect"}, localizer.Text(px::ui::TextId::Connect),
                             {.icon = px::ui::VectorIcon::Connect, .width = buttonWidth, .disabled = directPassword_.empty()})) {
        const auto target = std::move(directTarget_);
        const auto password = std::move(directPassword_);
        directTarget_.clear();
        directPassword_.clear();
        ImGui::CloseCurrentPopup();
        port_->Connect(target, password, directViewOnly_);
        directViewOnly_ = false;
    }
}

void RemoteControlPage::Draw(const px::ui::Localizer& localizer, const px::desktop::PlatformIconAtlas& platformIcons) {
    const auto state = port_->Snapshot();
    px::ui::PageTitle(localizer.Text(px::ui::TextId::ThisDevice));
    DrawIdentity(state, localizer);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px::ui::Scale(13.0F));
    px::ui::PageTitle(localizer.Text(px::ui::TextId::RemoteControl));
    ImGui::SameLine(0.0F, px::ui::Scale(14.0F));
    const std::string managerState{std::string{localizer.Text(px::ui::TextId::ManagerService)} + " " +
                                   std::string{localizer.Text(state.managerOnline ? px::ui::TextId::Online : px::ui::TextId::Offline)}};
    px::ui::StatusBadge(managerState, state.managerOnline ? px::ui::BadgeVariant::Success : px::ui::BadgeVariant::Destructive);
    DrawConnections(state, localizer, platformIcons);
    qrDialog_.Draw(localizer);
}

} // namespace px::panel::ui
