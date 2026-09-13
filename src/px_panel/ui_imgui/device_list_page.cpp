#include "device_list_page.h"

#include "px_desktop_shell/platform_icon_atlas.h"

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
#include <string>
#include <utility>
#include <vector>

namespace px::panel::ui {
namespace {

constexpr ImGuiWindowFlags detailCardFlags{ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};

std::string Lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool Matches(const RemoteDeviceCard& device, const std::string& query) {
    if (query.empty())
        return true;
    const std::string lowered{Lowercase(query)};
    return Lowercase(device.name).contains(lowered) || Lowercase(device.deviceId).contains(lowered) || Lowercase(device.host).contains(lowered);
}

std::string DeviceIdentity(const RemoteDeviceCard& device) {
    return device.deviceId.empty() ? device.streamId : device.deviceId;
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

void DetailRow(const std::string_view label, const std::string& value) {
    const float rowHeight{px::ui::Scale(35.0F)};
    const auto centerText = [rowHeight] {
        const float offset{std::max(0.0F, (rowHeight - ImGui::GetTextLineHeight()) * 0.5F - ImGui::GetStyle().CellPadding.y)};
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
    };
    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
    ImGui::TableNextColumn();
    centerText();
    ImGui::TextDisabled("%.*s", static_cast<int>(label.size()), label.data());
    ImGui::TableNextColumn();
    centerText();
    px::ui::StrongText(value.empty() ? "--" : value);
}

} // namespace

DeviceListPage::DeviceListPage(std::shared_ptr<RemoteControlPort> port) : port_{port}, deviceActions_{std::move(port), "device-list"} {}

void DeviceListPage::Draw(const px::ui::Localizer& localizer, const px::desktop::PlatformIconAtlas& platformIcons) {
    const auto state = port_->Snapshot();
    std::vector<RemoteDeviceCard> filtered{};
    for (const auto& device : state.devices) {
        if (Matches(device, search_))
            filtered.push_back(device);
    }
    if (selectedDeviceId_.empty() && !filtered.empty())
        selectedDeviceId_ = DeviceIdentity(filtered.front());

    px::ui::PageTitle(localizer.Text(px::ui::TextId::DeviceList));
    const float columnGap{px::ui::Scale(12.0F)};
    const float availableWidth{ImGui::GetContentRegionAvail().x};
    const float listWidth{std::clamp(availableWidth * 0.39F, px::ui::Scale(270.0F), px::ui::Scale(330.0F))};
    const float contentHeight{ImGui::GetContentRegionAvail().y};
    {
        px::ui::CardScope master{{"DeviceMaster"}, {listWidth, contentHeight}};
        if (master.Visible()) {
            px::ui::SectionTitle(std::string{localizer.Text(px::ui::TextId::AllDevices)} + " (" + std::to_string(filtered.size()) + ")");
            const float refreshSize{px::ui::Scale(32.0F)};
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - refreshSize);
            if (px::ui::IconAction({"device-list-refresh"}, px::ui::VectorIcon::Refresh, localizer.Text(px::ui::TextId::Refresh),
                                   {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconSm, .circular = true})) {
                port_->Refresh();
            }
            ImGui::Dummy({0.0F, px::ui::Scale(4.0F)});
            static_cast<void>(px::ui::SearchField({"device-search"}, search_, localizer.Text(px::ui::TextId::SearchDevices)));
            ImGui::Dummy({0.0F, px::ui::Scale(4.0F)});
            for (const auto& device : filtered) {
                const std::string identity{DeviceIdentity(device)};
                const bool selected{identity == selectedDeviceId_};
                ImGui::PushID(identity.c_str());
                const ImVec2 rowSize{ImGui::GetContentRegionAvail().x, px::ui::Scale(60.0F)};
                const ImVec2 rowMinimum{ImGui::GetCursorScreenPos()};
                ImGui::InvisibleButton("##device-row", rowSize);
                const bool hovered{ImGui::IsItemHovered()};
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    selectedDeviceId_ = identity;
                }
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    deviceActions_.Start(device, false);
                const ImVec2 minimum{ImGui::GetItemRectMin()};
                auto& draw = *ImGui::GetWindowDrawList();
                const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
                if (selected || hovered) {
                    draw.AddRectFilled(rowMinimum, {rowMinimum.x + rowSize.x, rowMinimum.y + rowSize.y},
                                       ImGui::GetColorU32(selected ? tokens.accent : tokens.muted), px::ui::Scale(8.0F));
                }
                const float iconSize{px::ui::Scale(28.0F)};
                platformIcons.Draw(device.platform, {minimum.x + px::ui::Scale(10.0F), minimum.y + px::ui::Scale(16.0F)}, iconSize,
                                   ImGui::GetColorU32(tokens.primary));
                draw.AddCircleFilled({ImGui::GetItemRectMax().x - px::ui::Scale(11.0F), minimum.y + px::ui::Scale(12.0F)}, px::ui::Scale(3.0F),
                                     ImGui::GetColorU32(device.online ? tokens.success : tokens.mutedForeground));
                const std::string name{device.name.empty() ? identity : device.name};
                draw.AddText({minimum.x + px::ui::Scale(50.0F), minimum.y + px::ui::Scale(8.0F)}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                const std::string address{DeviceAddress(device)};
                draw.AddText({minimum.x + px::ui::Scale(50.0F), minimum.y + px::ui::Scale(31.0F)}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                             address.c_str());
                {
                    px::ui::ContextMenuScope menu{{"DeviceActions"}};
                    if (menu.Open()) {
                        deviceActions_.DrawContextMenu(device, localizer);
                    }
                }
                ImGui::PopID();
            }
        }
    }

    ImGui::SameLine(0.0F, columnGap);
    {
        px::ui::CardScope detail{{"DeviceDetail"}, {availableWidth - listWidth - columnGap, contentHeight}, detailCardFlags};
        if (detail.Visible()) {
            const std::string selectedDeviceId{selectedDeviceId_};
            const auto selected = std::ranges::find_if(
                filtered, [selectedDeviceId](const RemoteDeviceCard& device) { return DeviceIdentity(device) == selectedDeviceId; });
            if (selected == filtered.end()) {
                px::ui::EmptyState(px::ui::VectorIcon::Monitor, localizer.Text(px::ui::TextId::NoDeviceSelected), {});
            } else {
                const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
                const ImVec2 heroStart{ImGui::GetCursorScreenPos()};
                const float heroWidth{ImGui::GetContentRegionAvail().x};
                const float heroIconSize{px::ui::Scale(44.0F)};
                auto& draw = *ImGui::GetWindowDrawList();
                draw.AddRectFilled(heroStart, {heroStart.x + heroIconSize, heroStart.y + heroIconSize}, ImGui::GetColorU32(tokens.accent),
                                   px::ui::Scale(9.0F));
                platformIcons.Draw(selected->platform, {heroStart.x + px::ui::Scale(8.0F), heroStart.y + px::ui::Scale(8.0F)},
                                   heroIconSize - px::ui::Scale(16.0F), ImGui::GetColorU32(tokens.primary));
                ImGui::SetCursorScreenPos({heroStart.x + heroIconSize + px::ui::Scale(12.0F), heroStart.y});
                px::ui::StrongText(selected->name.empty() ? DeviceAddress(*selected) : selected->name);
                const std::string_view statusText{localizer.Text(selected->online ? px::ui::TextId::Online : px::ui::TextId::Offline)};
                const float statusWidth{ImGui::CalcTextSize(statusText.data(), statusText.data() + statusText.size()).x + px::ui::Scale(24.0F)};
                ImGui::SetCursorScreenPos({heroStart.x + heroWidth - statusWidth, heroStart.y});
                px::ui::StatusBadge(statusText, selected->online ? px::ui::BadgeVariant::Success : px::ui::BadgeVariant::Secondary);
                ImGui::SetCursorScreenPos({heroStart.x + heroIconSize + px::ui::Scale(12.0F), heroStart.y + px::ui::Scale(25.0F)});
                ImGui::AlignTextToFramePadding();
                const std::string deviceAddress{DeviceAddress(*selected)};
                px::ui::MutedText(deviceAddress);
                ImGui::SameLine(0.0F, px::ui::Scale(6.0F));
                if (px::ui::IconAction({"device-list-copy-id"}, px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy),
                                       {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconXs})) {
                    port_->CopyText(selected->deviceId.empty() ? selected->host : selected->deviceId);
                }
                ImGui::SetCursorScreenPos({heroStart.x, heroStart.y + heroIconSize + px::ui::Scale(14.0F)});

                const float actionGap{ImGui::GetStyle().ItemSpacing.x};
                const float primaryWidth{(ImGui::GetContentRegionAvail().x - actionGap * 2.0F) / 3.0F};
                if (px::ui::ActionButton({"device-list-control"}, localizer.Text(px::ui::TextId::StartControl),
                                         {.icon = px::ui::VectorIcon::Play, .width = primaryWidth}))
                    deviceActions_.Start(*selected, false);
                ImGui::SameLine();
                if (px::ui::ActionButton({"device-list-view"}, localizer.Text(px::ui::TextId::ViewOnly),
                                         {.variant = px::ui::ButtonVariant::Outline, .icon = px::ui::VectorIcon::Eye, .width = primaryWidth}))
                    deviceActions_.Start(*selected, true);
                ImGui::SameLine();
                if (px::ui::ActionButton(
                        {"device-list-files"}, localizer.Text(px::ui::TextId::FileTransfer),
                        {.variant = px::ui::ButtonVariant::Outline, .icon = px::ui::VectorIcon::FileTransfer, .width = primaryWidth}))
                    deviceActions_.FileTransfer(*selected);

                ImGui::Dummy({0.0F, px::ui::Scale(10.0F)});
                px::ui::SectionTitle(localizer.Text(px::ui::TextId::DeviceDetails));
                px::ui::HorizontalSeparator();
                if (ImGui::BeginTable("DeviceDetailsTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
                    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(112.0F));
                    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                    DetailRow(localizer.Text(px::ui::TextId::DeviceId), FormatDeviceId(selected->deviceId));
                    DetailRow(localizer.Text(px::ui::TextId::DeviceName), selected->name);
                    DetailRow(localizer.Text(px::ui::TextId::Host), selected->host);
                    DetailRow(localizer.Text(px::ui::TextId::Port), selected->port > 0 ? std::to_string(selected->port) : std::string{});
                    DetailRow(localizer.Text(px::ui::TextId::ApplicationState),
                              std::string{localizer.Text(selected->online ? px::ui::TextId::Online : px::ui::TextId::Offline)});
                    ImGui::EndTable();
                }

                const float commandHeight{px::ui::Scale(36.0F)};
                const float bottomInset{px::ui::Scale(15.0F)};
                const float commandGap{ImGui::GetStyle().ItemSpacing.x};
                const float contentMinimumX{ImGui::GetWindowContentRegionMin().x};
                const float contentMaximumY{ImGui::GetWindowContentRegionMax().y};
                const float contentWidth{ImGui::GetWindowContentRegionMax().x - contentMinimumX};
                const float commandWidth{(contentWidth - commandGap * 2.0F) / 3.0F};
                ImGui::SetCursorPos({contentMinimumX, contentMaximumY - bottomInset - commandHeight});
                if (px::ui::ActionButton({"device-list-lock"}, localizer.Text(px::ui::TextId::LockDevice),
                                         {.variant = px::ui::ButtonVariant::Outline,
                                          .icon = px::ui::VectorIcon::Shield,
                                          .width = commandWidth,
                                          .height = commandHeight,
                                          .disabled = !selected->online})) {
                    deviceActions_.Command(*selected, RemoteDeviceCommand::Lock);
                }
                ImGui::SameLine(0.0F, commandGap);
                if (px::ui::ActionButton({"device-list-restart"}, localizer.Text(px::ui::TextId::Restart),
                                         {.variant = px::ui::ButtonVariant::Outline,
                                          .icon = px::ui::VectorIcon::Restart,
                                          .width = commandWidth,
                                          .height = commandHeight,
                                          .disabled = !selected->online})) {
                    deviceActions_.Command(*selected, RemoteDeviceCommand::Restart);
                }
                ImGui::SameLine(0.0F, commandGap);
                if (px::ui::ActionButton({"device-list-shutdown"}, localizer.Text(px::ui::TextId::Shutdown),
                                         {.variant = px::ui::ButtonVariant::Destructive,
                                          .icon = px::ui::VectorIcon::Power,
                                          .width = commandWidth,
                                          .height = commandHeight,
                                          .disabled = !selected->online})) {
                    deviceActions_.Command(*selected, RemoteDeviceCommand::Shutdown);
                }
            }
        }
    }
    deviceActions_.DrawDialogs(localizer);
}

} // namespace px::panel::ui
