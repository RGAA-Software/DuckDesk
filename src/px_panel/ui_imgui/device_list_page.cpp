#include "device_list_page.h"

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

void DetailRow(const std::string_view label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%.*s", static_cast<int>(label.size()), label.data());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value.empty() ? "--" : value.c_str());
}

} // namespace

DeviceListPage::DeviceListPage(std::shared_ptr<RemoteControlPort> port) : port_{port}, deviceActions_{std::move(port), "device-list"} {}

void DeviceListPage::Draw(const px::ui::Localizer& localizer) {
    const auto state = port_->Snapshot();
    std::vector<RemoteDeviceCard> filtered{};
    for (const auto& device : state.devices) {
        if (Matches(device, search_))
            filtered.push_back(device);
    }
    if (selectedDeviceId_.empty() && !filtered.empty())
        selectedDeviceId_ = filtered.front().deviceId;

    px::ui::PageTitle(localizer.Text(px::ui::TextId::DeviceList));
    ImGui::Spacing();
    static_cast<void>(px::ui::TextField({"device-search"}, search_, localizer.Text(px::ui::TextId::SearchDevices), {.width = px::ui::Scale(360.0F)}));
    ImGui::SameLine();
    if (px::ui::ActionButton({"device-list-refresh"}, localizer.Text(px::ui::TextId::Refresh),
                             {.variant = px::ui::ButtonVariant::Outline, .icon = px::ui::VectorIcon::Refresh}))
        port_->Refresh();
    ImGui::Spacing();

    const float listWidth{std::max(px::ui::Scale(280.0F), ImGui::GetContentRegionAvail().x * 0.38F)};
    const float contentHeight{ImGui::GetContentRegionAvail().y};
    {
        px::ui::CardScope master{{"DeviceMaster"}, {listWidth, contentHeight}};
        if (master.Visible()) {
            px::ui::SectionTitle(std::string{localizer.Text(px::ui::TextId::AllDevices)} + " (" + std::to_string(filtered.size()) + ")");
            px::ui::HorizontalSeparator();
            for (const auto& device : filtered) {
                const std::string identity{device.deviceId.empty() ? device.streamId : device.deviceId};
                const bool selected{identity == selectedDeviceId_};
                ImGui::PushID(identity.c_str());
                if (px::ui::SelectableRow({"device-row"}, "##device", selected, ImGuiSelectableFlags_AllowDoubleClick,
                                          {0.0F, px::ui::Scale(48.0F)})) {
                    selectedDeviceId_ = identity;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        deviceActions_.Start(device, false);
                }
                const ImVec2 minimum{ImGui::GetItemRectMin()};
                auto& draw = *ImGui::GetWindowDrawList();
                const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
                draw.AddCircleFilled({minimum.x + px::ui::Scale(10.0F), minimum.y + px::ui::Scale(15.0F)}, px::ui::Scale(3.0F),
                                     ImGui::GetColorU32(device.online ? tokens.success : tokens.mutedForeground));
                const std::string name{device.name.empty() ? identity : device.name};
                draw.AddText({minimum.x + px::ui::Scale(21.0F), minimum.y + px::ui::Scale(5.0F)}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                draw.AddText({minimum.x + px::ui::Scale(21.0F), minimum.y + px::ui::Scale(26.0F)}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                             identity.c_str());
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

    ImGui::SameLine();
    {
        px::ui::CardScope detail{{"DeviceDetail"}, {0.0F, contentHeight}};
        if (detail.Visible()) {
            const std::string selectedDeviceId{selectedDeviceId_};
            const auto selected = std::ranges::find_if(filtered, [selectedDeviceId](const RemoteDeviceCard& device) {
                return (device.deviceId.empty() ? device.streamId : device.deviceId) == selectedDeviceId;
            });
            if (selected == filtered.end()) {
                px::ui::EmptyState(px::ui::VectorIcon::Monitor, localizer.Text(px::ui::TextId::NoDeviceSelected), {});
            } else {
                px::ui::SectionTitle(localizer.Text(px::ui::TextId::DeviceDetails));
                px::ui::HorizontalSeparator();
                ImGui::Text("%s", selected->name.empty() ? selected->deviceId.c_str() : selected->name.c_str());
                if (ImGui::BeginTable("DeviceDetailsTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(100.0F));
                    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                    DetailRow(localizer.Text(px::ui::TextId::DeviceId), selected->deviceId);
                    DetailRow(localizer.Text(px::ui::TextId::DeviceName), selected->name);
                    DetailRow(localizer.Text(px::ui::TextId::Host), selected->host);
                    DetailRow(localizer.Text(px::ui::TextId::Port), selected->port > 0 ? std::to_string(selected->port) : std::string{});
                    DetailRow(localizer.Text(px::ui::TextId::ApplicationState),
                              std::string{localizer.Text(selected->online ? px::ui::TextId::Online : px::ui::TextId::Offline)});
                    ImGui::EndTable();
                }
                if (px::ui::ActionButton({"device-list-control"}, localizer.Text(px::ui::TextId::StartControl),
                                         {.size = px::ui::WidgetSize::Sm, .icon = px::ui::VectorIcon::Play}))
                    deviceActions_.Start(*selected, false);
                ImGui::SameLine();
                if (px::ui::ActionButton(
                        {"device-list-view"}, localizer.Text(px::ui::TextId::ViewOnly),
                        {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Sm, .icon = px::ui::VectorIcon::Eye}))
                    deviceActions_.Start(*selected, true);
                if (px::ui::ActionButton(
                        {"device-list-files"}, localizer.Text(px::ui::TextId::FileTransfer),
                        {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Sm, .icon = px::ui::VectorIcon::FileTransfer}))
                    deviceActions_.FileTransfer(*selected);
                ImGui::SameLine();
                if (px::ui::IconAction({"device-list-copy-id"}, px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy),
                                       {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconSm}))
                    port_->CopyText(selected->deviceId);
                ImGui::SameLine();
                if (px::ui::IconAction({"device-list-edit"}, px::ui::VectorIcon::Pencil, localizer.Text(px::ui::TextId::EditDevice),
                                       {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconSm}))
                    deviceActions_.Edit(*selected);
                ImGui::SameLine();
                if (px::ui::IconAction({"device-list-delete"}, px::ui::VectorIcon::Trash, localizer.Text(px::ui::TextId::Delete),
                                       {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconSm}))
                    deviceActions_.Remove(*selected);
            }
        }
    }
    deviceActions_.DrawDialogs(localizer);
}

} // namespace px::panel::ui
