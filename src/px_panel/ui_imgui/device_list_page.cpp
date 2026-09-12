#include "device_list_page.h"

#include "px_ui/layout_metrics.h"
#include "px_ui/vector_icon.h"

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

    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::DeviceList).data());
    ImGui::SetNextItemWidth(px::ui::Scale(330.0F));
    ImGui::InputTextWithHint("##device-search", localizer.Text(px::ui::TextId::SearchDevices).data(), &search_);
    ImGui::SameLine();
    if (px::ui::IconButton(px::ui::VectorIcon::Refresh, localizer.Text(px::ui::TextId::Refresh), "device-list-refresh"))
        port_->Refresh();
    ImGui::Spacing();

    const float listWidth{std::max(px::ui::Scale(280.0F), ImGui::GetContentRegionAvail().x * 0.38F)};
    ImGui::BeginChild("DeviceMaster", {listWidth, 0.0F}, ImGuiChildFlags_Borders);
    ImGui::SeparatorText((std::string{localizer.Text(px::ui::TextId::AllDevices)} + " (" + std::to_string(filtered.size()) + ")").c_str());
    for (const auto& device : filtered) {
        const std::string identity{device.deviceId.empty() ? device.streamId : device.deviceId};
        const bool selected{identity == selectedDeviceId_};
        ImGui::PushID(identity.c_str());
        if (ImGui::Selectable("##device", selected, ImGuiSelectableFlags_AllowDoubleClick, {0.0F, px::ui::Scale(54.0F)})) {
            selectedDeviceId_ = identity;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                deviceActions_.Start(device, false);
        }
        const ImVec2 minimum{ImGui::GetItemRectMin()};
        auto& draw = *ImGui::GetWindowDrawList();
        draw.AddCircleFilled({minimum.x + px::ui::Scale(13.0F), minimum.y + px::ui::Scale(18.0F)}, px::ui::Scale(4.0F),
                             ImGui::GetColorU32(device.online ? ImVec4{0.15F, 0.82F, 0.34F, 1.0F} : ImVec4{0.55F, 0.57F, 0.62F, 1.0F}));
        const std::string name{device.name.empty() ? identity : device.name};
        draw.AddText({minimum.x + px::ui::Scale(25.0F), minimum.y + px::ui::Scale(7.0F)}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
        draw.AddText({minimum.x + px::ui::Scale(25.0F), minimum.y + px::ui::Scale(29.0F)}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                     identity.c_str());
        if (ImGui::BeginPopupContextItem("DeviceActions")) {
            deviceActions_.DrawContextMenu(device, localizer);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("DeviceDetail", {0.0F, 0.0F}, ImGuiChildFlags_Borders);
    const std::string selectedDeviceId{selectedDeviceId_};
    const auto selected = std::ranges::find_if(filtered, [selectedDeviceId](const RemoteDeviceCard& device) {
        return (device.deviceId.empty() ? device.streamId : device.deviceId) == selectedDeviceId;
    });
    if (selected == filtered.end()) {
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::NoDeviceSelected).data());
        ImGui::EndChild();
        deviceActions_.DrawDialogs(localizer);
        return;
    }
    ImGui::SeparatorText(localizer.Text(px::ui::TextId::DeviceDetails).data());
    ImGui::Text("%s", selected->name.empty() ? selected->deviceId.c_str() : selected->name.c_str());
    if (ImGui::BeginTable("DeviceDetailsTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(120.0F));
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        DetailRow(localizer.Text(px::ui::TextId::DeviceId), selected->deviceId);
        DetailRow(localizer.Text(px::ui::TextId::DeviceName), selected->name);
        DetailRow(localizer.Text(px::ui::TextId::Host), selected->host);
        DetailRow(localizer.Text(px::ui::TextId::Port), selected->port > 0 ? std::to_string(selected->port) : std::string{});
        DetailRow(localizer.Text(px::ui::TextId::ApplicationState),
                  std::string{localizer.Text(selected->online ? px::ui::TextId::Online : px::ui::TextId::Offline)});
        ImGui::EndTable();
    }
    if (px::ui::IconButton(px::ui::VectorIcon::Play, localizer.Text(px::ui::TextId::StartControl), "device-list-control"))
        deviceActions_.Start(*selected, false);
    ImGui::SameLine();
    if (px::ui::IconButton(px::ui::VectorIcon::Eye, localizer.Text(px::ui::TextId::ViewOnly), "device-list-view"))
        deviceActions_.Start(*selected, true);
    if (px::ui::IconButton(px::ui::VectorIcon::FileTransfer, localizer.Text(px::ui::TextId::FileTransfer), "device-list-files"))
        deviceActions_.FileTransfer(*selected);
    ImGui::SameLine();
    if (px::ui::IconButton(px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy), "device-list-copy-id"))
        port_->CopyText(selected->deviceId);
    ImGui::SameLine();
    if (px::ui::IconButton(px::ui::VectorIcon::Pencil, localizer.Text(px::ui::TextId::EditDevice), "device-list-edit"))
        deviceActions_.Edit(*selected);
    ImGui::SameLine();
    if (px::ui::IconButton(px::ui::VectorIcon::Trash, localizer.Text(px::ui::TextId::Delete), "device-list-delete"))
        deviceActions_.Remove(*selected);
    ImGui::EndChild();
    deviceActions_.DrawDialogs(localizer);
}

} // namespace px::panel::ui
