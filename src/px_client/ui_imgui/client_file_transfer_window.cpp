#include "client_file_transfer_window.h"

#include "client_text.h"
#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/theme_tokens.h"
#include "px_desktop_shell/platform_icon_atlas.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <format>
#include <optional>
#include <span>
#include <string_view>

namespace px::client::imgui {
namespace {

std::string FormatSize(const std::uint64_t bytes) {
    constexpr std::uint64_t kib{1024U};
    constexpr std::uint64_t mib{kib * 1024U};
    constexpr std::uint64_t gib{mib * 1024U};
    if (bytes >= gib)
        return std::format("{:.1f} GB", static_cast<double>(bytes) / static_cast<double>(gib));
    if (bytes >= mib)
        return std::format("{:.1f} MB", static_cast<double>(bytes) / static_cast<double>(mib));
    if (bytes >= kib)
        return std::format("{:.1f} KB", static_cast<double>(bytes) / static_cast<double>(kib));
    return std::format("{} B", bytes);
}

std::string FormatModified(const std::uint64_t seconds) {
    if (seconds == 0U)
        return {};
    const std::time_t value{static_cast<std::time_t>(seconds)};
    std::tm local{};
    if (localtime_s(&local, &value) != 0)
        return {};
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
}

std::string RemoteParent(const std::string& path) {
    if (path.empty() || path == "/")
        return "/";
    if (path.size() == 3U && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
        return "/";
    const auto last = path.find_last_not_of("/\\");
    if (last == std::string::npos)
        return {};
    const auto separator = path.find_last_of("/\\", last);
    if (separator == std::string::npos)
        return {};
    return separator == 0U ? path.substr(0, 1) : path.substr(0, separator);
}

std::string RemoteJoin(const std::string& directory, const std::string& name) {
    if (directory.empty())
        return name;
    return directory.ends_with('/') || directory.ends_with('\\') ? directory + name : directory + "/" + name;
}

bool ValidEntryName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:*?\"<>|") == std::string::npos;
}

std::string DeviceAddress(const std::string_view deviceId, const std::string_view host) {
    const bool isDeviceCode{!deviceId.empty() &&
                            std::ranges::all_of(deviceId, [](const char character) { return character >= '0' && character <= '9'; })};
    if (!isDeviceCode)
        return std::string{host};
    if (deviceId.size() == 9U)
        return std::string{deviceId.substr(0, 3U)} + " " + std::string{deviceId.substr(3U, 3U)} + " " + std::string{deviceId.substr(6U, 3U)};
    return std::string{deviceId};
}

void CenterTableCellText(const float rowHeight) {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0F, (rowHeight - ImGui::GetTextLineHeight()) * 0.5F));
}

void DrawFileTableHeaders(const std::array<std::string_view, 3>& labels, const float rowHeight) {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, rowHeight);
    for (std::size_t column{}; column < labels.size(); ++column) {
        if (!ImGui::TableSetColumnIndex(static_cast<int>(column)))
            continue;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px::ui::Scale(10.0F));
        ImGui::PushID(static_cast<int>(column));
        ImGui::TableHeader(labels[column].data()); // NOLINT(gammaray-raw-pointer-boundary): Dear ImGui transient text ABI
        ImGui::PopID();
    }
}

void DrawTransferTableHeaders(const std::array<std::string_view, 8>& labels, const float rowHeight) {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, rowHeight);
    for (std::size_t column{}; column < labels.size(); ++column) {
        if (!ImGui::TableSetColumnIndex(static_cast<int>(column)))
            continue;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px::ui::Scale(10.0F));
        ImGui::PushID(static_cast<int>(column));
        ImGui::TableHeader(labels[column].data()); // NOLINT(gammaray-raw-pointer-boundary): Dear ImGui transient text ABI
        ImGui::PopID();
    }
}

ClientText LocationText(const ClientFileLocationKind kind) {
    switch (kind) {
    case ClientFileLocationKind::Computer:
        return ClientText::ThisComputer;
    case ClientFileLocationKind::Home:
        return ClientText::UserProfile;
    case ClientFileLocationKind::Desktop:
        return ClientText::Desktop;
    case ClientFileLocationKind::Downloads:
        return ClientText::Downloads;
    case ClientFileLocationKind::Documents:
        return ClientText::Documents;
    case ClientFileLocationKind::Pictures:
        return ClientText::Pictures;
    case ClientFileLocationKind::Music:
        return ClientText::Music;
    case ClientFileLocationKind::Videos:
        return ClientText::Videos;
    case ClientFileLocationKind::Drive:
        return ClientText::ThisComputer;
    }
    return ClientText::ThisComputer;
}

struct FilePathChoice final {
    std::string label{};
    std::string path{};
};

struct FilePathSegment final {
    std::string label{};
    std::string path{};
};

std::vector<FilePathSegment> BuildPathSegments(const std::string_view path, const std::string_view computerLabel) {
    std::vector<FilePathSegment> result{{std::string{computerLabel}, "/"}};
    if (path.empty() || path == "/")
        return result;

    const bool windowsPath{path.size() >= 2U && path[1] == ':'};
    std::size_t cursor{};
    std::string accumulated{};
    if (windowsPath) {
        accumulated = std::string{path.substr(0, 2U)} + "\\";
        result.push_back({std::string{path.substr(0, 2U)}, accumulated});
        cursor = 2U;
    }
    while (cursor < path.size() && (path[cursor] == '/' || path[cursor] == '\\'))
        ++cursor;
    while (cursor < path.size()) {
        const auto separator = path.find_first_of("/\\", cursor);
        const auto end = separator == std::string_view::npos ? path.size() : separator;
        const std::string label{path.substr(cursor, end - cursor)};
        if (!label.empty()) {
            if (!windowsPath && accumulated.empty())
                accumulated = "/";
            if (!accumulated.empty() && !accumulated.ends_with('/') && !accumulated.ends_with('\\'))
                accumulated += windowsPath ? "\\" : "/";
            accumulated += label;
            result.push_back({label, accumulated});
        }
        if (separator == std::string_view::npos)
            break;
        cursor = separator + 1U;
        while (cursor < path.size() && (path[cursor] == '/' || path[cursor] == '\\'))
            ++cursor;
    }
    return result;
}

std::optional<std::string> DrawFilePathBar(const std::string_view id, const std::string_view path, const std::string_view computerLabel,
                                           const std::span<const FilePathChoice> locations, const float width, std::string& editablePath,
                                           bool& editing, bool& requestFocus) {
    const auto tokens = px::ui::CurrentThemeTokens();
    const float height{px::ui::Scale(35.0F)};
    constexpr float edgePadding{5.0F};
    constexpr float dropdownWidth{32.0F};
    constexpr float separatorWidth{16.0F};
    std::optional<std::string> navigation{};
    const std::string scopeId{id};
    ImGui::PushID(scopeId.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tokens.input);
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
    const bool visible{
        ImGui::BeginChild("path-bar", {width, height}, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)};
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (visible) {
        const float contentWidth{std::max(1.0F, width - dropdownWidth - edgePadding * 2.0F)};
        if (editing) {
            ImGui::SetCursorPos({edgePadding, 2.0F});
            if (requestFocus) {
                ImGui::SetKeyboardFocusHere();
                requestFocus = false;
            }
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{7.0F, 7.0F});
            ImGui::SetNextItemWidth(contentWidth);
            if (ImGui::InputText("##editable-path", &editablePath, ImGuiInputTextFlags_EnterReturnsTrue)) {
                navigation = editablePath;
                editing = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                editing = false;
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
        } else {
            const auto segments = BuildPathSegments(path, computerLabel);
            std::vector<float> segmentWidths{};
            segmentWidths.reserve(segments.size());
            float requiredWidth{};
            for (std::size_t index{}; index < segments.size(); ++index) {
                const float segmentWidth{std::clamp(ImGui::CalcTextSize(segments[index].label.c_str()).x + 16.0F, 30.0F, contentWidth)};
                segmentWidths.push_back(segmentWidth);
                requiredWidth += segmentWidth + (index == 0U ? 0.0F : separatorWidth);
            }
            std::size_t firstVisible{};
            constexpr float ellipsisWidth{32.0F};
            while (firstVisible + 1U < segments.size() && requiredWidth > contentWidth) {
                requiredWidth -= segmentWidths[firstVisible] + separatorWidth;
                ++firstVisible;
            }
            float cursorX{edgePadding};
            if (firstVisible > 0U) {
                ImGui::SetCursorPos({cursorX, 2.0F});
                if (ImGui::InvisibleButton("breadcrumb-overflow", {ellipsisWidth, height - 4.0F}))
                    ImGui::OpenPopup("path-locations");
                if (ImGui::IsItemHovered())
                    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(tokens.accent),
                                                              4.0F);
                const ImVec2 itemMinimum{ImGui::GetItemRectMin()};
                const ImVec2 itemMaximum{ImGui::GetItemRectMax()};
                const ImVec2 textSize{ImGui::CalcTextSize("...")};
                ImGui::GetWindowDrawList()->AddText({itemMinimum.x + (itemMaximum.x - itemMinimum.x - textSize.x) * 0.5F,
                                                     itemMinimum.y + (itemMaximum.y - itemMinimum.y - textSize.y) * 0.5F},
                                                    ImGui::GetColorU32(tokens.mutedForeground), "...");
                cursorX += ellipsisWidth;
            }
            for (std::size_t index{firstVisible}; index < segments.size(); ++index) {
                if (cursorX > edgePadding) {
                    px::ui::DrawVectorIcon(px::ui::VectorIcon::ChevronRight,
                                           {ImGui::GetWindowPos().x + cursorX + 1.0F, ImGui::GetWindowPos().y + 11.0F}, 12.0F,
                                           ImGui::GetColorU32(tokens.mutedForeground));
                    cursorX += separatorWidth;
                }
                const float available{std::max(30.0F, contentWidth - cursorX + edgePadding)};
                const float segmentWidth{std::min(segmentWidths[index], available)};
                ImGui::SetCursorPos({cursorX, 2.0F});
                const std::string segmentId{"breadcrumb-" + std::to_string(index)};
                const bool pressed{ImGui::InvisibleButton(segmentId.c_str(), {segmentWidth, height - 4.0F})};
                if (ImGui::IsItemHovered())
                    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(tokens.accent),
                                                              4.0F);
                const ImVec2 itemMinimum{ImGui::GetItemRectMin()};
                const ImVec2 itemMaximum{ImGui::GetItemRectMax()};
                const ImVec2 textSize{ImGui::CalcTextSize(segments[index].label.c_str())};
                ImGui::GetWindowDrawList()->PushClipRect(itemMinimum, itemMaximum, true);
                ImGui::GetWindowDrawList()->AddText({itemMinimum.x + 8.0F, itemMinimum.y + (itemMaximum.y - itemMinimum.y - textSize.y) * 0.5F},
                                                    ImGui::GetColorU32(index + 1U == segments.size() ? tokens.foreground : tokens.mutedForeground),
                                                    segments[index].label.c_str());
                ImGui::GetWindowDrawList()->PopClipRect();
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    editablePath = std::string{path};
                    editing = true;
                    requestFocus = true;
                } else if (pressed) {
                    navigation = segments[index].path;
                }
                cursorX += segmentWidth;
            }
        }

        ImGui::SetCursorPos({width - dropdownWidth, 2.0F});
        if (ImGui::InvisibleButton("location-dropdown", {dropdownWidth - 2.0F, height - 4.0F}))
            ImGui::OpenPopup("path-locations");
        if (ImGui::IsItemHovered())
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(tokens.accent), 4.0F);
        const ImVec2 center{(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5F,
                            (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F + 1.0F};
        ImGui::GetWindowDrawList()->AddTriangleFilled({center.x - 4.0F, center.y - 2.0F}, {center.x + 4.0F, center.y - 2.0F},
                                                      {center.x, center.y + 3.0F}, ImGui::GetColorU32(tokens.mutedForeground));

        const px::ui::PopupMenuScope popup{{"path-locations"}};
        if (popup.Open()) {
            for (std::size_t index{}; index < locations.size(); ++index) {
                const std::string optionId{"path-location-" + std::to_string(index)};
                if (px::ui::MenuAction({optionId}, locations[index].label, {.selected = locations[index].path == path}))
                    navigation = locations[index].path;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
    return navigation;
}

void DrawComputerIdentity(const px::desktop::PlatformIconAtlas& icons, const px::ui::DevicePlatform platform, const std::string_view title,
                          const std::string_view subtitle, const std::string_view detail = {}) {
    const auto tokens = px::ui::CurrentThemeTokens();
    const ImVec2 topLeft{ImGui::GetCursorScreenPos()};
    constexpr float tileSize{50.0F};
    ImGui::Dummy({tileSize, tileSize});
    ImGui::GetWindowDrawList()->AddRectFilled(topLeft, {topLeft.x + tileSize, topLeft.y + tileSize}, ImGui::GetColorU32(tokens.accent), 8.0F);
    if (platform == px::ui::DevicePlatform::Unknown)
        px::ui::DrawVectorIcon(px::ui::VectorIcon::Monitor, {topLeft.x + 13.0F, topLeft.y + 13.0F}, 24.0F, ImGui::GetColorU32(tokens.primary));
    else
        icons.Draw(platform, {topLeft.x + 9.0F, topLeft.y + 9.0F}, 32.0F, IM_COL32_WHITE);
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float lineCount{detail.empty() ? 2.0F : 3.0F};
    const float textHeight{ImGui::GetTextLineHeight() * lineCount};
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0F, (tileSize - textHeight) * 0.5F));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0.0F});
    px::ui::StrongText(title);
    px::ui::MutedText(subtitle);
    if (!detail.empty())
        px::ui::MutedText(detail);
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

} // namespace

ClientFileTransferWindow::ClientFileTransferWindow(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session,
                                                   const ClientLaunchConfig& config, const bool english)
    : shell_{shell}, session_{std::move(session)}, localIdentity_{DeviceAddress(config.localDeviceId, config.localHost)},
      remoteName_{config.streamName}, remoteIdentity_{DeviceAddress(config.remoteDeviceId, config.host)},
      remotePlatform_{px::ui::ParseDevicePlatform(config.remotePlatform)}, localPath_{localFiles_.Path()}, english_{english} {}

std::vector<ClientFileListItem> ClientFileTransferWindow::VisibleLocalItems() const {
    std::vector<ClientFileListItem> items{};
    for (const auto& entry : localFiles_.Entries()) {
        if (showHiddenLocal_ || !entry.hidden)
            items.push_back({entry.path, entry.name, entry.size, entry.modifiedTime, entry.directory});
    }
    if (localFiles_.Path() != "/")
        localSort_.Apply(items);
    return items;
}

std::vector<ClientFileListItem> ClientFileTransferWindow::VisibleRemoteItems() const {
    std::vector<ClientFileListItem> items{};
    for (const auto& entry : session_->RemoteEntries()) {
        if (showHiddenRemote_ || !entry.hidden)
            items.push_back({entry.path, entry.name, entry.size, entry.modifiedTime, entry.directory});
    }
    if (remotePath_ != "/")
        remoteSort_.Apply(items);
    return items;
}

void ClientFileTransferWindow::NavigateRemote(std::string path, const bool addHistory) {
    if (addHistory && !remotePath_.empty() && path != remotePath_)
        remoteHistory_.push_back(remotePath_);
    remoteSelection_.Clear();
    static_cast<void>(session_->ListRemoteDirectory(path, showHiddenRemote_));
}

void ClientFileTransferWindow::DrawLocalLocationPicker(const float width) {
    const auto text = [english = english_](const ClientText id) { return ClientTextValue(id, english).data(); };
    const auto& locations = localFiles_.Locations();
    std::vector<FilePathChoice> choices{};
    choices.reserve(locations.size());
    for (const auto& location : locations) {
        choices.push_back(
            {.label = location.kind == ClientFileLocationKind::Drive ? location.label : text(LocationText(location.kind)), .path = location.path});
    }
    if (const auto destination = DrawFilePathBar("local-location", localFiles_.Path(), text(ClientText::ThisComputer), choices, width, localPath_,
                                                 localPathEditing_, localPathFocusRequested_)) {
        if (localFiles_.Navigate(*destination))
            localSelection_.Clear();
    }
}

void ClientFileTransferWindow::DrawRemoteLocationPicker(const float width) {
    const auto text = [english = english_](const ClientText id) { return ClientTextValue(id, english).data(); };
    const auto locations = session_->RemoteLocations();
    std::vector<FilePathChoice> choices{{text(ClientText::ThisComputer), "/"}};
    choices.reserve(locations.size() + 1U);
    for (const auto& location : locations) {
        if (!location.directory || location.path.empty())
            continue;
        choices.push_back({location.name, location.path});
    }
    if (const auto destination = DrawFilePathBar("remote-location", remotePath_, text(ClientText::ThisComputer), choices, width, remotePath_,
                                                 remotePathEditing_, remotePathFocusRequested_))
        NavigateRemote(*destination, true);
}

void ClientFileTransferWindow::HandleInput(const px::desktop::DesktopInputEvent& event) {
    if (event.type == SDL_EVENT_DROP_FILE && !event.text.empty()) {
        if (session_->StartUpload(event.text, remotePath_) > 0) {
            toasts_.Push({.title = std::string{ClientTextValue(ClientText::DroppedUploadStarted, english_)},
                          .description = std::filesystem::path{event.text}.filename().string(),
                          .variant = px::ui::FeedbackVariant::Success});
        }
        return;
    }
    if (event.type != SDL_EVENT_QUIT && event.type != SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        return;
    shell_.get().CancelCloseRequest();
    shell_.get().RequestShowAndRaise();
    openCloseConfirmation_ = true;
}

void ClientFileTransferWindow::Draw() {
    if (!shown_) {
        shown_ = true;
        shell_.get().RequestShowAndRaise();
    }
    const auto text = [english = english_](const ClientText id) { return ClientTextValue(id, english).data(); };
    if (const auto operation = session_->TakeRemoteFileOperationResult()) {
        if (operation->success) {
            static_cast<void>(session_->ListRemoteDirectory(remotePath_, showHiddenRemote_));
            remoteSelection_.Clear();
            toasts_.Push({.title = text(ClientText::OperationSucceeded), .variant = px::ui::FeedbackVariant::Success});
        } else {
            toasts_.Push({.title = text(ClientText::OperationFailed), .description = operation->error, .variant = px::ui::FeedbackVariant::Error});
        }
    }
    const auto snapshot = session_->Snapshot();
    const float edgeSpacing{px::ui::Scale(10.0F)};
    ImGui::SetCursorPos({ImGui::GetCursorPosX() + edgeSpacing, ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + edgeSpacing});
    const ImVec2 paddedAvailable{ImGui::GetContentRegionAvail()};
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
    const bool contentVisible{ImGui::BeginChild("file-manager-content",
                                                {std::max(1.0F, paddedAvailable.x - edgeSpacing), std::max(1.0F, paddedAvailable.y - edgeSpacing)},
                                                ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)};
    ImGui::PopStyleVar();
    if (contentVisible) {
        const ImVec2 contentAvailable{ImGui::GetContentRegionAvail()};
        const float queueHeight{std::clamp(contentAvailable.y * 0.24F, px::ui::Scale(150.0F), px::ui::Scale(220.0F))};
        const float filePaneHeight{std::max(1.0F, contentAvailable.y - queueHeight - edgeSpacing)};
        const float filePaneWidth{std::max(1.0F, (contentAvailable.x - edgeSpacing) * 0.5F)};
        const float filePaneTop{ImGui::GetCursorPosY()};

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
        if (ImGui::BeginChild("local-file-pane-slot", {filePaneWidth, filePaneHeight}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            DrawLocalPane();
        ImGui::EndChild();
        ImGui::SameLine(0.0F, edgeSpacing);
        if (ImGui::BeginChild("remote-file-pane-slot", {filePaneWidth, filePaneHeight}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            DrawRemotePane();
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SetCursorPosY(filePaneTop + filePaneHeight + edgeSpacing);
        DrawTransferQueue();
    }
    ImGui::EndChild();
    DrawConnectionFailure(snapshot);
    DrawFileOperationDialog();
    DrawCloseConfirmation();
    toasts_.Draw();

    if (const auto overwrite = session_->PendingOverwrite()) {
        px::ui::OpenModal({"standalone-file-overwrite"});
        const px::ui::ModalScope modal{{"standalone-file-overwrite"}, 520.0F};
        if (modal.Open()) {
            static_cast<void>(
                px::ui::DialogHeader({"standalone-overwrite-close"}, text(ClientText::DestinationExists), overwrite->path,
                                     {.icon = px::ui::VectorIcon::TriangleAlert, .tone = px::ui::BadgeVariant::Warning, .closeable = false}));
            static_cast<void>(px::ui::CheckboxField({"standalone-overwrite-all"}, text(ClientText::ApplyToAll), applyOverwriteToAll_));
            px::ui::DialogFooter(272.0F);
            if (px::ui::ActionButton({"standalone-overwrite-skip"}, text(ClientText::Skip),
                                     {.variant = px::ui::ButtonVariant::Outline, .width = 130.0F})) {
                static_cast<void>(session_->ConfirmOverwrite(false, applyOverwriteToAll_));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (px::ui::ActionButton({"standalone-overwrite-confirm"}, text(ClientText::Overwrite), {.width = 130.0F})) {
                static_cast<void>(session_->ConfirmOverwrite(true, applyOverwriteToAll_));
                ImGui::CloseCurrentPopup();
            }
        }
    }
}

void ClientFileTransferWindow::DrawLocalPane() {
    const auto text = [english = english_](const ClientText id) { return ClientTextValue(id, english).data(); };
    const float height{ImGui::GetContentRegionAvail().y};
    const float navigationHeight{px::ui::Scale(35.0F)};
    px::ui::CardScope card{{"local-file-pane"}, {0.0F, height}};
    if (!card.Visible())
        return;
    DrawComputerIdentity(shell_.get().PlatformIcons(), px::ui::DevicePlatform::Windows, text(ClientText::LocalComputer), "Windows", localIdentity_);
    const float navigationTop{ImGui::GetCursorPosY()};
    if (px::ui::IconAction({"local-back"}, px::ui::VectorIcon::ArrowLeft, text(ClientText::Back),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .height = navigationHeight}))
        if (localFiles_.NavigateBack())
            localSelection_.Clear();
    ImGui::SameLine();
    ImGui::SetCursorPosY(navigationTop);
    if (px::ui::IconAction({"local-up"}, px::ui::VectorIcon::ArrowUp, text(ClientText::Up),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .height = navigationHeight}))
        if (localFiles_.NavigateUp())
            localSelection_.Clear();
    ImGui::SameLine();
    ImGui::SetCursorPosY(navigationTop);
    const float localAddressWidth{
        std::max(px::ui::Scale(120.0F), ImGui::GetContentRegionAvail().x - navigationHeight - ImGui::GetStyle().ItemSpacing.x)};
    DrawLocalLocationPicker(localAddressWidth);
    ImGui::SameLine();
    ImGui::SetCursorPosY(navigationTop);
    if (px::ui::IconAction({"local-refresh"}, px::ui::VectorIcon::Refresh, text(ClientText::Refresh),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .height = navigationHeight}))
        static_cast<void>(localFiles_.Refresh());
    if (px::ui::IconAction({"local-home"}, px::ui::VectorIcon::Home, text(ClientText::Home),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon})) {
        if (localFiles_.NavigateHome())
            localSelection_.Clear();
    }
    ImGui::SameLine();
    if (px::ui::IconAction({"local-new-folder"}, px::ui::VectorIcon::Plus, text(ClientText::NewFolder),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        BeginOperation(FileOperation::CreateLocal);
    ImGui::SameLine();
    if (px::ui::IconAction({"local-delete"}, px::ui::VectorIcon::Trash, text(ClientText::Delete),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .disabled = localSelection_.Empty()}))
        BeginOperation(FileOperation::DeleteLocal);
    ImGui::SameLine();
    if (px::ui::IconAction({"local-more"}, px::ui::VectorIcon::More, text(ClientText::More),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        ImGui::OpenPopup("local-file-more");
    const bool connected{session_->Snapshot().state == ClientConnectionState::Connected};
    constexpr float transferButtonWidth{96.0F};
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - transferButtonWidth));
    if (px::ui::ActionButton({"send-selected"}, text(ClientText::Send),
                             {.icon = px::ui::VectorIcon::ChevronRight,
                              .iconTrailing = true,
                              .width = transferButtonWidth,
                              .disabled = localSelection_.Empty() || !connected})) {
        for (const auto& path : localSelection_.Paths())
            static_cast<void>(session_->StartUpload(path, remotePath_));
    }
    {
        const px::ui::PopupMenuScope localMore{{"local-file-more"}};
        if (localMore.Open()) {
            if (px::ui::MenuAction({"local-show-hidden"}, text(ClientText::ShowHiddenFiles),
                                   {.icon = showHiddenLocal_ ? px::ui::VectorIcon::EyeOff : px::ui::VectorIcon::Eye, .selected = showHiddenLocal_}))
                showHiddenLocal_ = !showHiddenLocal_;
            if (px::ui::MenuAction({"local-select-all"}, text(ClientText::SelectAll), {.icon = px::ui::VectorIcon::Check}))
                localSelection_.SelectAll(VisibleLocalItems());
            if (px::ui::MenuAction({"local-unselect-all"}, text(ClientText::UnselectAll), {.icon = px::ui::VectorIcon::Minus}))
                localSelection_.Clear();
        }
    }
    if (!localFiles_.Error().empty())
        px::ui::FieldError(localFiles_.Error());

    const bool localComputerView{localFiles_.Path() == "/"};
    const ImGuiTableFlags localTableFlags{ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                          (localComputerView ? ImGuiTableFlags_None : ImGuiTableFlags_Sortable)};
    if (ImGui::BeginTable("local-files", 3, localTableFlags, {0.0F, ImGui::GetContentRegionAvail().y})) {
        ImGui::TableSetupColumn(text(ClientText::Name), localComputerView ? ImGuiTableColumnFlags_None : ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn(text(ClientText::Modified), ImGuiTableColumnFlags_WidthFixed, 132.0F);
        ImGui::TableSetupColumn(text(ClientText::Size), ImGuiTableColumnFlags_WidthFixed, 82.0F);
        const float rowHeight{px::ui::Scale(35.0F)};
        const float headerPaddingY{std::max(0.0F, (rowHeight - ImGui::GetTextLineHeight()) * 0.5F)};
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {px::ui::Scale(10.0F), headerPaddingY});
        DrawFileTableHeaders(
            {std::string_view{text(ClientText::Name)}, std::string_view{text(ClientText::Modified)}, std::string_view{text(ClientText::Size)}},
            rowHeight);
        ImGui::PopStyleVar();
        if (!localComputerView) {
            ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs(); // NOLINT(gammaray-raw-pointer-boundary): Dear ImGui borrowed ABI
            if (sortSpecs && sortSpecs->SpecsCount > 0 && sortSpecs->SpecsDirty) {
                const auto& specification = sortSpecs->Specs[0];
                localSort_.column = specification.ColumnIndex == 1   ? ClientFileSortColumn::Modified
                                    : specification.ColumnIndex == 2 ? ClientFileSortColumn::Size
                                                                     : ClientFileSortColumn::Name;
                localSort_.ascending = specification.SortDirection != ImGuiSortDirection_Descending;
                sortSpecs->SpecsDirty = false;
            }
        }
        const auto visibleItems = VisibleLocalItems();
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {ImGui::GetStyle().CellPadding.x, 0.0F});
        for (std::size_t index{}; index < visibleItems.size(); ++index) {
            const auto& entry = visibleItems[index];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableNextColumn();
            const bool selected{localSelection_.Contains(entry.path)};
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0.0F});
            const bool rowPressed{
                px::ui::SelectableIconRow({entry.path}, entry.directory ? px::ui::VectorIcon::Folder : px::ui::VectorIcon::File, entry.name, selected,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, {0.0F, rowHeight})};
            ImGui::PopStyleVar();
            if (rowPressed) {
                localSelection_.Select(index, entry.path, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift, visibleItems);
                if (entry.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    if (localFiles_.Navigate(entry.path))
                        localSelection_.Clear();
            }
            {
                const std::string contextId{"local-entry-context##" + entry.path};
                const px::ui::ContextMenuScope context{{contextId}};
                if (context.Open()) {
                    localSelection_.SelectOnly(index, entry.path);
                    if (px::ui::MenuAction({"rename-local-entry"}, text(ClientText::Rename), {.icon = px::ui::VectorIcon::Pencil}))
                        BeginOperation(FileOperation::RenameLocal, entry.name);
                }
            }
            ImGui::TableNextColumn();
            CenterTableCellText(rowHeight);
            ImGui::TextDisabled("%s", FormatModified(entry.modifiedTime).c_str());
            ImGui::TableNextColumn();
            if (!entry.directory) {
                CenterTableCellText(rowHeight);
                ImGui::TextDisabled("%s", FormatSize(entry.size).c_str());
            }
        }
        ImGui::PopStyleVar();
        ImGui::EndTable();
    }
}

void ClientFileTransferWindow::DrawRemotePane() {
    const auto text = [english = english_](const ClientText id) { return ClientTextValue(id, english).data(); };
    const float height{ImGui::GetContentRegionAvail().y};
    const float navigationHeight{px::ui::Scale(35.0F)};
    px::ui::CardScope card{{"remote-file-pane"}, {0.0F, height}};
    if (!card.Visible())
        return;
    DrawComputerIdentity(shell_.get().PlatformIcons(), remotePlatform_, text(ClientText::RemoteComputer), remoteName_, remoteIdentity_);
    const float navigationTop{ImGui::GetCursorPosY()};
    if (px::ui::IconAction({"remote-back"}, px::ui::VectorIcon::ArrowLeft, text(ClientText::Back),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .height = navigationHeight}) &&
        !remoteHistory_.empty()) {
        const std::string previous{remoteHistory_.back()};
        remoteHistory_.pop_back();
        NavigateRemote(previous, false);
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(navigationTop);
    if (px::ui::IconAction({"remote-up"}, px::ui::VectorIcon::ArrowUp, text(ClientText::Up),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .height = navigationHeight}))
        NavigateRemote(RemoteParent(remotePath_), true);
    ImGui::SameLine();
    ImGui::SetCursorPosY(navigationTop);
    const float remoteAddressWidth{
        std::max(px::ui::Scale(120.0F), ImGui::GetContentRegionAvail().x - navigationHeight - ImGui::GetStyle().ItemSpacing.x)};
    DrawRemoteLocationPicker(remoteAddressWidth);
    ImGui::SameLine();
    ImGui::SetCursorPosY(navigationTop);
    if (px::ui::IconAction({"remote-refresh"}, px::ui::VectorIcon::Refresh, text(ClientText::Refresh),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .height = navigationHeight}))
        NavigateRemote(remotePath_, false);
    if (!remotePathEditing_)
        remotePath_ = session_->RemotePath();
    const bool connected{session_->Snapshot().state == ClientConnectionState::Connected};
    constexpr float transferButtonWidth{96.0F};
    if (px::ui::ActionButton(
            {"receive-selected"}, text(ClientText::Receive),
            {.icon = px::ui::VectorIcon::ChevronLeft, .width = transferButtonWidth, .disabled = remoteSelection_.Empty() || !connected})) {
        for (const auto& path : remoteSelection_.Paths())
            static_cast<void>(session_->StartDownload(path, localFiles_.Path()));
    }
    const px::ui::UiMetrics metrics{px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const float remoteToolsWidth{metrics.controlDefault * 4.0F + metrics.spacingSm * 3.0F};
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - remoteToolsWidth));
    if (px::ui::IconAction({"remote-home"}, px::ui::VectorIcon::Home, text(ClientText::Home),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon})) {
        const auto locations = session_->RemoteLocations();
        const auto home = std::ranges::find_if(locations, [](const ClientRemoteEntry& location) {
            return location.directory && !(location.path.size() == 3U && location.path[1] == ':');
        });
        NavigateRemote(home == locations.end() ? "/" : home->path, true);
    }
    ImGui::SameLine();
    if (px::ui::IconAction({"remote-new-folder"}, px::ui::VectorIcon::Plus, text(ClientText::NewFolder),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        BeginOperation(FileOperation::CreateRemote);
    ImGui::SameLine();
    if (px::ui::IconAction({"remote-delete"}, px::ui::VectorIcon::Trash, text(ClientText::Delete),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .disabled = remoteSelection_.Empty()}))
        BeginOperation(FileOperation::DeleteRemote);
    ImGui::SameLine();
    if (px::ui::IconAction({"remote-more"}, px::ui::VectorIcon::More, text(ClientText::More),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        ImGui::OpenPopup("remote-file-more");
    {
        const px::ui::PopupMenuScope remoteMore{{"remote-file-more"}};
        if (remoteMore.Open()) {
            if (px::ui::MenuAction(
                    {"remote-show-hidden"}, text(ClientText::ShowHiddenFiles),
                    {.icon = showHiddenRemote_ ? px::ui::VectorIcon::EyeOff : px::ui::VectorIcon::Eye, .selected = showHiddenRemote_})) {
                showHiddenRemote_ = !showHiddenRemote_;
                NavigateRemote(remotePath_, false);
            }
            if (px::ui::MenuAction({"remote-select-all"}, text(ClientText::SelectAll), {.icon = px::ui::VectorIcon::Check}))
                remoteSelection_.SelectAll(VisibleRemoteItems());
            if (px::ui::MenuAction({"remote-unselect-all"}, text(ClientText::UnselectAll), {.icon = px::ui::VectorIcon::Minus}))
                remoteSelection_.Clear();
        }
    }

    const bool remoteComputerView{remotePath_ == "/"};
    const ImGuiTableFlags remoteTableFlags{ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                           (remoteComputerView ? ImGuiTableFlags_None : ImGuiTableFlags_Sortable)};
    if (ImGui::BeginTable("remote-files-standalone", 3, remoteTableFlags, {0.0F, ImGui::GetContentRegionAvail().y})) {
        ImGui::TableSetupColumn(text(ClientText::Name), remoteComputerView ? ImGuiTableColumnFlags_None : ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn(text(ClientText::Modified), ImGuiTableColumnFlags_WidthFixed, 132.0F);
        ImGui::TableSetupColumn(text(ClientText::Size), ImGuiTableColumnFlags_WidthFixed, 82.0F);
        const float rowHeight{px::ui::Scale(35.0F)};
        const float headerPaddingY{std::max(0.0F, (rowHeight - ImGui::GetTextLineHeight()) * 0.5F)};
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {px::ui::Scale(10.0F), headerPaddingY});
        DrawFileTableHeaders(
            {std::string_view{text(ClientText::Name)}, std::string_view{text(ClientText::Modified)}, std::string_view{text(ClientText::Size)}},
            rowHeight);
        ImGui::PopStyleVar();
        if (!remoteComputerView) {
            ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs(); // NOLINT(gammaray-raw-pointer-boundary): Dear ImGui borrowed ABI
            if (sortSpecs && sortSpecs->SpecsCount > 0 && sortSpecs->SpecsDirty) {
                const auto& specification = sortSpecs->Specs[0];
                remoteSort_.column = specification.ColumnIndex == 1   ? ClientFileSortColumn::Modified
                                     : specification.ColumnIndex == 2 ? ClientFileSortColumn::Size
                                                                      : ClientFileSortColumn::Name;
                remoteSort_.ascending = specification.SortDirection != ImGuiSortDirection_Descending;
                sortSpecs->SpecsDirty = false;
            }
        }
        const auto visibleItems = VisibleRemoteItems();
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {ImGui::GetStyle().CellPadding.x, 0.0F});
        for (std::size_t index{}; index < visibleItems.size(); ++index) {
            const auto& entry = visibleItems[index];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableNextColumn();
            const bool selected{remoteSelection_.Contains(entry.path)};
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0.0F});
            const bool rowPressed{
                px::ui::SelectableIconRow({entry.path}, entry.directory ? px::ui::VectorIcon::Folder : px::ui::VectorIcon::File, entry.name, selected,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, {0.0F, rowHeight})};
            ImGui::PopStyleVar();
            if (rowPressed) {
                remoteSelection_.Select(index, entry.path, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift, visibleItems);
                if (entry.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    NavigateRemote(entry.path, true);
            }
            {
                const std::string contextId{"remote-entry-context##" + entry.path};
                const px::ui::ContextMenuScope context{{contextId}};
                if (context.Open()) {
                    remoteSelection_.SelectOnly(index, entry.path);
                    if (px::ui::MenuAction({"rename-remote-entry"}, text(ClientText::Rename), {.icon = px::ui::VectorIcon::Pencil}))
                        BeginOperation(FileOperation::RenameRemote, entry.name);
                }
            }
            ImGui::TableNextColumn();
            CenterTableCellText(rowHeight);
            ImGui::TextDisabled("%s", FormatModified(entry.modifiedTime).c_str());
            ImGui::TableNextColumn();
            if (!entry.directory) {
                CenterTableCellText(rowHeight);
                ImGui::TextDisabled("%s", FormatSize(entry.size).c_str());
            }
        }
        ImGui::PopStyleVar();
        ImGui::EndTable();
    }
}

void ClientFileTransferWindow::DrawTransferQueue() {
    const auto text = [english = english_](const ClientText id) { return ClientTextValue(id, english).data(); };
    const float height{ImGui::GetContentRegionAvail().y};
    px::ui::CardScope card{{"file-transfer-queue"}, {0.0F, height}};
    if (!card.Visible())
        return;
    const auto jobs = session_->TransferJobs();
    const bool hasCompleted{std::ranges::any_of(jobs, [](const ClientTransferJob& job) { return job.done || !job.error.empty(); })};
    const px::ui::UiMetrics metrics{px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const float titleTop{ImGui::GetCursorPosY()};
    const float titleHeight{metrics.controlSm};
    ImGui::SetCursorPosY(titleTop + std::max(0.0F, (titleHeight - ImGui::GetTextLineHeight() * 1.08F) * 0.5F));
    px::ui::SectionTitle(text(ClientText::TransferQueue));
    const float clearWidth{std::max(px::ui::Scale(108.0F), ImGui::CalcTextSize(text(ClientText::ClearCompleted)).x + metrics.spacingMd * 2.0F)};
    ImGui::SetCursorPos({ImGui::GetContentRegionMax().x - clearWidth, titleTop + (titleHeight - metrics.controlXs) * 0.5F});
    if (px::ui::ActionButton(
            {"clear-completed-jobs"}, text(ClientText::ClearCompleted),
            {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Xs, .width = clearWidth, .disabled = !hasCompleted}))
        session_->RemoveCompletedTransfers();
    ImGui::SetCursorPosY(titleTop + titleHeight + metrics.spacingSm);

    constexpr int columnCount{8};
    const float rowHeight{px::ui::Scale(35.0F)};
    const ImVec2 tableMinimum{ImGui::GetCursorScreenPos()};
    const float tableWidth{ImGui::GetContentRegionAvail().x};
    const float tableHeight{ImGui::GetContentRegionAvail().y};
    if (!ImGui::BeginTable("transfer-queue-table", columnCount, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, {0.0F, tableHeight}))
        return;
    ImGui::TableSetupColumn(text(ClientText::Name), ImGuiTableColumnFlags_WidthStretch, 1.4F);
    ImGui::TableSetupColumn(text(ClientText::Type), ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(76.0F));
    ImGui::TableSetupColumn(text(ClientText::Files), ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(68.0F));
    ImGui::TableSetupColumn(text(ClientText::Progress), ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn(text(ClientText::Size), ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(190.0F));
    ImGui::TableSetupColumn(text(ClientText::Speed), ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(90.0F));
    ImGui::TableSetupColumn(text(ClientText::Status), ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(120.0F));
    ImGui::TableSetupColumn(text(ClientText::Actions), ImGuiTableColumnFlags_WidthFixed, px::ui::Scale(176.0F));
    const float headerPaddingY{std::max(0.0F, (rowHeight - ImGui::GetTextLineHeight()) * 0.5F)};
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {px::ui::Scale(10.0F), headerPaddingY});
    DrawTransferTableHeaders({std::string_view{text(ClientText::Name)}, std::string_view{text(ClientText::Type)},
                              std::string_view{text(ClientText::Files)}, std::string_view{text(ClientText::Progress)},
                              std::string_view{text(ClientText::Size)}, std::string_view{text(ClientText::Speed)},
                              std::string_view{text(ClientText::Status)}, std::string_view{text(ClientText::Actions)}},
                             rowHeight);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {ImGui::GetStyle().CellPadding.x, 0.0F});
    for (const auto& job : jobs) {
        ImGui::PushID(job.id);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
        ImGui::TableNextColumn();
        const std::string jobName{job.name.empty() ? std::format("#{}", job.id) : job.name};
        static_cast<void>(px::ui::SelectableRow({"transfer-job-row"}, jobName, false,
                                                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, {0.0F, rowHeight}));
        if (!job.error.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", job.error.c_str());

        ImGui::TableNextColumn();
        CenterTableCellText(rowHeight);
        ImGui::TextDisabled("%s", text(job.download ? ClientText::Download : ClientText::Upload));

        ImGui::TableNextColumn();
        if (job.fileCount > 0) {
            CenterTableCellText(rowHeight);
            ImGui::TextDisabled("%d / %d", std::min(job.fileNumber + 1, job.fileCount), job.fileCount);
        }

        const float progress{
            job.totalBytes == 0U ? 0.0F : std::clamp(static_cast<float>(job.completedBytes) / static_cast<float>(job.totalBytes), 0.0F, 1.0F)};
        ImGui::TableNextColumn();
        const float progressTop{ImGui::GetCursorPosY()};
        const float percentWidth{px::ui::Scale(42.0F)};
        ImGui::SetCursorPosY(progressTop + std::max(0.0F, (rowHeight - metrics.spacingSm) * 0.5F));
        px::ui::Progress(progress, std::max(px::ui::Scale(24.0F), ImGui::GetContentRegionAvail().x - percentWidth));
        ImGui::SameLine();
        ImGui::SetCursorPosY(progressTop + std::max(0.0F, (rowHeight - ImGui::GetTextLineHeight()) * 0.5F));
        ImGui::TextDisabled("%.0f%%", progress * 100.0F);

        ImGui::TableNextColumn();
        CenterTableCellText(rowHeight);
        ImGui::TextDisabled("%s / %s", FormatSize(job.completedBytes).c_str(), FormatSize(job.totalBytes).c_str());

        ImGui::TableNextColumn();
        CenterTableCellText(rowHeight);
        ImGui::TextDisabled("%.1f KB/s", job.bytesPerSecond / 1024.0);

        ImGui::TableNextColumn();
        if (!job.error.empty()) {
            CenterTableCellText(rowHeight);
            const auto tokens = px::ui::CurrentThemeTokens();
            ImGui::PushStyleColor(ImGuiCol_Text, tokens.destructive);
            ImGui::TextUnformatted(job.error.c_str());
            ImGui::PopStyleColor();
        } else if (job.done) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0F, (rowHeight - ImGui::GetTextLineHeight() - metrics.spacingXs * 2.0F) * 0.5F));
            px::ui::StatusBadge(text(ClientText::Completed), px::ui::BadgeVariant::Success);
        }

        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0F, (rowHeight - metrics.controlXs) * 0.5F));
        if (!job.done && px::ui::ActionButton({"cancel-queue-job"}, text(ClientText::Cancel),
                                              {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Xs}))
            static_cast<void>(session_->CancelTransfer(job.id));
        if (!job.error.empty()) {
            ImGui::SameLine();
            if (px::ui::ActionButton({"resume-queue-job"}, text(ClientText::Resume),
                                     {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Xs}))
                static_cast<void>(session_->ResumeTransfer(job.id));
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    if (jobs.empty()) {
        const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
        const ImVec2 emptyMinimum{tableMinimum.x, tableMinimum.y + rowHeight};
        const ImVec2 emptyMaximum{tableMinimum.x + tableWidth, tableMinimum.y + tableHeight};
        const float iconSize{metrics.iconLg * 1.35F};
        const std::string_view emptyText{text(ClientText::NoTransfers)};
        const ImVec2 textSize{ImGui::CalcTextSize(emptyText.data(), emptyText.data() + emptyText.size())};
        const float contentHeight{iconSize + metrics.spacingSm + textSize.y};
        const float contentTop{emptyMinimum.y + std::max(0.0F, (emptyMaximum.y - emptyMinimum.y - contentHeight) * 0.5F)};
        ImDrawList& draw{*ImGui::GetWindowDrawList()};
        draw.PushClipRect(emptyMinimum, emptyMaximum, false);
        px::ui::DrawVectorIcon(draw, px::ui::VectorIcon::Inbox, {emptyMinimum.x + (emptyMaximum.x - emptyMinimum.x - iconSize) * 0.5F, contentTop},
                               iconSize, ImGui::GetColorU32(tokens.mutedForeground));
        draw.AddText({emptyMinimum.x + (emptyMaximum.x - emptyMinimum.x - textSize.x) * 0.5F, contentTop + iconSize + metrics.spacingSm},
                     ImGui::GetColorU32(tokens.mutedForeground), emptyText.data(), emptyText.data() + emptyText.size());
        draw.PopClipRect();
    }
    ImGui::EndTable();
}

void ClientFileTransferWindow::DrawConnectionFailure(const ClientSessionSnapshot& snapshot) {
    if (snapshot.state != ClientConnectionState::Rejected)
        return;
    if (!errorPopupOpened_) {
        ImGui::OpenPopup("File transfer failed###standalone-file-transfer-failed");
        errorPopupOpened_ = true;
    }
    const px::ui::ModalScope modal{{"File transfer failed###standalone-file-transfer-failed"},
                                   540.0F,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings};
    if (!modal.Open())
        return;
    const auto text = [english = english_](const ClientText id) { return ClientTextValue(id, english).data(); };
    const std::string_view description{snapshot.failure == ClientConnectionFailure::RemoteAccessDisabled
                                           ? ClientTextValue(ClientText::RemoteAccessDisabled, english_)
                                           : std::string_view{snapshot.status}};
    static_cast<void>(
        px::ui::DialogHeader({"standalone-file-error-close"}, text(ClientText::ConnectionFailed), description,
                             {.icon = px::ui::VectorIcon::TriangleAlert, .tone = px::ui::BadgeVariant::Destructive, .closeable = false}));
    px::ui::DialogFooter(150.0F);
    if (px::ui::ActionButton({"standalone-file-error-ok"}, text(ClientText::Ok), {.width = 150.0F}))
        shell_.get().RequestExit();
}

void ClientFileTransferWindow::BeginOperation(const FileOperation operation, std::string value) {
    operation_ = operation;
    operationValue_ = std::move(value);
    operationError_.clear();
    openOperationDialog_ = true;
}

void ClientFileTransferWindow::DrawFileOperationDialog() {
    if (operation_ == FileOperation::None)
        return;
    if (openOperationDialog_) {
        px::ui::OpenModal({"standalone-file-operation"});
        openOperationDialog_ = false;
    }
    const px::ui::ModalScope modal{{"standalone-file-operation"}, 440.0F};
    if (!modal.Open())
        return;
    const auto text = [english = english_](const ClientText id) { return ClientTextValue(id, english).data(); };
    const bool deleting{operation_ == FileOperation::DeleteLocal || operation_ == FileOperation::DeleteRemote};
    const bool creating{operation_ == FileOperation::CreateLocal || operation_ == FileOperation::CreateRemote};
    const ClientText title{deleting ? ClientText::Delete : creating ? ClientText::NewFolder : ClientText::Rename};
    static_cast<void>(px::ui::DialogHeader({"standalone-file-operation-close"}, text(title), deleting ? text(ClientText::ConfirmDelete) : "",
                                           {.icon = deleting   ? px::ui::VectorIcon::Trash
                                                    : creating ? px::ui::VectorIcon::Plus
                                                               : px::ui::VectorIcon::Pencil,
                                            .tone = deleting ? px::ui::BadgeVariant::Destructive : px::ui::BadgeVariant::Secondary,
                                            .closeable = false}));
    if (!deleting) {
        px::ui::FieldLabel(text(ClientText::ItemName));
        static_cast<void>(px::ui::TextField({"standalone-file-operation-name"}, operationValue_, {},
                                            {.invalid = !operationValue_.empty() && !ValidEntryName(operationValue_)}));
    }
    if (!operationError_.empty())
        px::ui::FieldError(operationError_);
    constexpr float buttonWidth{112.0F};
    px::ui::DialogFooter(buttonWidth * 2.0F + 12.0F);
    if (px::ui::ActionButton({"standalone-file-operation-cancel"}, text(ClientText::Cancel),
                             {.variant = px::ui::ButtonVariant::Outline, .width = buttonWidth})) {
        operation_ = FileOperation::None;
        ImGui::CloseCurrentPopup();
        return;
    }
    ImGui::SameLine();
    const bool valid{deleting || ValidEntryName(operationValue_)};
    const ClientText actionText{deleting ? ClientText::Delete : creating ? ClientText::Create : ClientText::Rename};
    if (!px::ui::ActionButton(
            {"standalone-file-operation-confirm"}, text(actionText),
            {.variant = deleting ? px::ui::ButtonVariant::Destructive : px::ui::ButtonVariant::Primary, .width = buttonWidth, .disabled = !valid}))
        return;

    bool accepted{};
    switch (operation_) {
    case FileOperation::CreateLocal:
        accepted = localFiles_.CreateDirectory(operationValue_);
        break;
    case FileOperation::CreateRemote:
        accepted = session_->CreateRemoteDirectory(RemoteJoin(remotePath_, operationValue_));
        break;
    case FileOperation::RenameLocal:
        accepted = !localSelection_.Paths().empty() && localFiles_.Rename(localSelection_.Paths().front(), operationValue_);
        break;
    case FileOperation::RenameRemote:
        accepted = !remoteSelection_.Paths().empty() && session_->RenameRemoteEntry(remoteSelection_.Paths().front(), operationValue_);
        break;
    case FileOperation::DeleteLocal:
        accepted = localFiles_.Remove(localSelection_.Paths());
        break;
    case FileOperation::DeleteRemote: {
        std::vector<ClientRemoteEntry> selected{};
        for (const auto& entry : session_->RemoteEntries()) {
            if (remoteSelection_.Contains(entry.path))
                selected.push_back(entry);
        }
        accepted = session_->RemoveRemoteEntries(selected);
        break;
    }
    case FileOperation::None:
        break;
    }
    if (!accepted) {
        operationError_ =
            operation_ == FileOperation::CreateLocal || operation_ == FileOperation::RenameLocal || operation_ == FileOperation::DeleteLocal
                ? localFiles_.Error()
                : text(ClientText::RemoteOperationQueueFailed);
        return;
    }
    if (operation_ == FileOperation::RenameLocal || operation_ == FileOperation::DeleteLocal)
        localSelection_.Clear();
    if (operation_ == FileOperation::RenameRemote || operation_ == FileOperation::DeleteRemote)
        remoteSelection_.Clear();
    operation_ = FileOperation::None;
    ImGui::CloseCurrentPopup();
}

void ClientFileTransferWindow::DrawCloseConfirmation() {
    if (openCloseConfirmation_) {
        px::ui::OpenModal({"standalone-file-close-confirmation"});
        openCloseConfirmation_ = false;
    }
    const px::ui::ModalScope modal{{"standalone-file-close-confirmation"}, 480.0F};
    if (!modal.Open())
        return;
    const auto text = [english = english_](const ClientText id) { return ClientTextValue(id, english).data(); };
    static_cast<void>(px::ui::DialogHeader({"standalone-file-close-header"}, text(ClientText::ExitFileTransfer),
                                           text(ClientText::ExitFileTransferDetail),
                                           {.icon = px::ui::VectorIcon::TriangleAlert, .tone = px::ui::BadgeVariant::Warning, .closeable = false}));
    px::ui::DialogFooter(236.0F);
    if (px::ui::ActionButton({"standalone-file-keep-open"}, text(ClientText::Cancel), {.variant = px::ui::ButtonVariant::Outline, .width = 112.0F})) {
        ImGui::CloseCurrentPopup();
        return;
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"standalone-file-close"}, text(ClientText::ExitFileTransfer),
                             {.variant = px::ui::ButtonVariant::Destructive, .width = 112.0F}))
        shell_.get().RequestExit();
}

} // namespace px::client::imgui
