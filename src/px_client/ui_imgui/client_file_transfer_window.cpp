#include "client_file_transfer_window.h"

#include "client_text.h"
#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <format>

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

bool MatchesSearch(const std::string& name, const std::string& search) {
    if (search.empty())
        return true;
    std::string foldedName{name};
    std::string foldedSearch{search};
    std::ranges::transform(foldedName, foldedName.begin(), [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    std::ranges::transform(foldedSearch, foldedSearch.begin(), [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return foldedName.contains(foldedSearch);
}

std::string RemoteParent(const std::string& path) {
    if (path.empty() || path == "/")
        return {};
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

} // namespace

ClientFileTransferWindow::ClientFileTransferWindow(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session,
                                                   std::string remoteName, const bool english)
    : shell_{shell}, session_{std::move(session)}, remoteName_{std::move(remoteName)}, localPath_{localFiles_.Path()}, english_{english} {}

void ClientFileTransferWindow::Draw() {
    if (!shown_) {
        shown_ = true;
        shell_.get().RequestShowAndRaise();
    }
    const auto text = [this](const ClientText id) { return ClientTextValue(id, english_).data(); };
    if (const auto operation = session_->TakeRemoteFileOperationResult()) {
        if (operation->success) {
            static_cast<void>(session_->ListRemoteDirectory(remotePath_));
            selectedRemote_.clear();
            toasts_.Push({.title = text(ClientText::OperationSucceeded), .variant = px::ui::FeedbackVariant::Success});
        } else {
            toasts_.Push({.title = text(ClientText::OperationFailed), .description = operation->error, .variant = px::ui::FeedbackVariant::Error});
        }
    }
    const auto snapshot = session_->Snapshot();
    px::ui::PageTitle(text(ClientText::FileTransfer));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", remoteName_.c_str());
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - 170.0F));
    px::ui::StatusBadge(text(snapshot.state == ClientConnectionState::Connected ? ClientText::FileTransferConnected : ClientText::Connecting),
                        snapshot.state == ClientConnectionState::Connected ? px::ui::BadgeVariant::Success : px::ui::BadgeVariant::Secondary);
    ImGui::Spacing();

    const float queueWidth{std::clamp(ImGui::GetContentRegionAvail().x * 0.25F, 260.0F, 360.0F)};
    if (ImGui::BeginTable("file-manager-columns", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("local", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("remote", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("queue", ImGuiTableColumnFlags_WidthFixed, queueWidth);
        ImGui::TableNextColumn();
        DrawLocalPane();
        ImGui::TableNextColumn();
        DrawRemotePane();
        ImGui::TableNextColumn();
        DrawTransferQueue();
        ImGui::EndTable();
    }
    DrawConnectionFailure(snapshot);
    DrawFileOperationDialog();
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
    const auto text = [this](const ClientText id) { return ClientTextValue(id, english_).data(); };
    const float height{ImGui::GetContentRegionAvail().y};
    px::ui::CardScope card{{"local-file-pane"}, {0.0F, height}};
    if (!card.Visible())
        return;
    px::ui::SectionTitle(text(ClientText::LocalComputer));
    ImGui::SameLine();
    ImGui::TextDisabled("Windows");
    if (px::ui::IconAction({"local-back"}, px::ui::VectorIcon::ChevronRight, text(ClientText::Back),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        static_cast<void>(localFiles_.NavigateBack());
    ImGui::SameLine();
    if (px::ui::IconAction({"local-up"}, px::ui::VectorIcon::Minus, text(ClientText::Up),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        static_cast<void>(localFiles_.NavigateUp());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-42.0F);
    if (px::ui::TextField({"local-address"}, localPath_, {}, {}, ImGuiInputTextFlags_EnterReturnsTrue))
        static_cast<void>(localFiles_.Navigate(localPath_));
    ImGui::SameLine();
    if (px::ui::IconAction({"local-refresh"}, px::ui::VectorIcon::Refresh, text(ClientText::Refresh),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        static_cast<void>(localFiles_.Refresh());
    localPath_ = localFiles_.Path();
    static_cast<void>(px::ui::SearchField({"local-search"}, localSearch_, text(ClientText::Search)));
    if (px::ui::IconAction({"local-new-folder"}, px::ui::VectorIcon::Plus, text(ClientText::NewFolder),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        BeginOperation(FileOperation::CreateLocal);
    ImGui::SameLine();
    if (px::ui::IconAction({"local-delete"}, px::ui::VectorIcon::Trash, text(ClientText::Delete),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .disabled = selectedLocal_.empty()}))
        BeginOperation(FileOperation::DeleteLocal);
    if (!localFiles_.Error().empty())
        px::ui::FieldError(localFiles_.Error());

    const float actionHeight{48.0F};
    if (ImGui::BeginTable("local-files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          {0.0F, ImGui::GetContentRegionAvail().y - actionHeight})) {
        ImGui::TableSetupColumn(text(ClientText::Name));
        ImGui::TableSetupColumn(text(ClientText::Modified), ImGuiTableColumnFlags_WidthFixed, 132.0F);
        ImGui::TableSetupColumn(text(ClientText::Size), ImGuiTableColumnFlags_WidthFixed, 82.0F);
        ImGui::TableHeadersRow();
        for (const auto& entry : localFiles_.Entries()) {
            if (!MatchesSearch(entry.name, localSearch_))
                continue;
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0F);
            ImGui::TableNextColumn();
            const bool selected{selectedLocal_ == entry.path};
            if (px::ui::SelectableRow({entry.path}, entry.name, selected, ImGuiSelectableFlags_SpanAllColumns, {0.0F, 30.0F})) {
                selectedLocal_ = entry.path;
                selectedLocalDirectory_ = entry.directory;
                if (entry.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    static_cast<void>(localFiles_.Navigate(entry.path));
            }
            const std::string contextId{"local-entry-context##" + entry.path};
            const px::ui::ContextMenuScope context{{contextId}};
            if (context.Open()) {
                selectedLocal_ = entry.path;
                selectedLocalDirectory_ = entry.directory;
                if (px::ui::MenuAction({"rename-local-entry"}, text(ClientText::Rename), {.icon = px::ui::VectorIcon::Pencil}))
                    BeginOperation(FileOperation::RenameLocal, entry.name);
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", FormatModified(entry.modifiedTime).c_str());
            ImGui::TableNextColumn();
            if (!entry.directory)
                ImGui::TextDisabled("%s", FormatSize(entry.size).c_str());
        }
        ImGui::EndTable();
    }
    const bool connected{session_->Snapshot().state == ClientConnectionState::Connected};
    if (px::ui::ActionButton({"send-selected"}, text(ClientText::Send),
                             {.icon = px::ui::VectorIcon::FileTransfer, .width = -1.0F, .disabled = selectedLocal_.empty() || !connected}))
        static_cast<void>(session_->StartUpload(selectedLocal_, remotePath_));
}

void ClientFileTransferWindow::DrawRemotePane() {
    const auto text = [this](const ClientText id) { return ClientTextValue(id, english_).data(); };
    const float height{ImGui::GetContentRegionAvail().y};
    px::ui::CardScope card{{"remote-file-pane"}, {0.0F, height}};
    if (!card.Visible())
        return;
    px::ui::SectionTitle(text(ClientText::RemoteComputer));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", remoteName_.c_str());
    if (px::ui::IconAction({"remote-up"}, px::ui::VectorIcon::Minus, text(ClientText::Up),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        static_cast<void>(session_->ListRemoteDirectory(RemoteParent(remotePath_)));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-42.0F);
    if (px::ui::TextField({"remote-address"}, remotePath_, {}, {}, ImGuiInputTextFlags_EnterReturnsTrue))
        static_cast<void>(session_->ListRemoteDirectory(remotePath_));
    ImGui::SameLine();
    if (px::ui::IconAction({"remote-refresh"}, px::ui::VectorIcon::Refresh, text(ClientText::Refresh),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        static_cast<void>(session_->ListRemoteDirectory(remotePath_));
    remotePath_ = session_->RemotePath();
    static_cast<void>(px::ui::SearchField({"remote-search"}, remoteSearch_, text(ClientText::Search)));
    if (px::ui::IconAction({"remote-new-folder"}, px::ui::VectorIcon::Plus, text(ClientText::NewFolder),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon}))
        BeginOperation(FileOperation::CreateRemote);
    ImGui::SameLine();
    if (px::ui::IconAction({"remote-delete"}, px::ui::VectorIcon::Trash, text(ClientText::Delete),
                           {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Icon, .disabled = selectedRemote_.empty()}))
        BeginOperation(FileOperation::DeleteRemote);

    const float actionHeight{48.0F};
    if (ImGui::BeginTable("remote-files-standalone", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          {0.0F, ImGui::GetContentRegionAvail().y - actionHeight})) {
        ImGui::TableSetupColumn(text(ClientText::Name));
        ImGui::TableSetupColumn(text(ClientText::Modified), ImGuiTableColumnFlags_WidthFixed, 132.0F);
        ImGui::TableSetupColumn(text(ClientText::Size), ImGuiTableColumnFlags_WidthFixed, 82.0F);
        ImGui::TableHeadersRow();
        for (const auto& entry : session_->RemoteEntries()) {
            if (!MatchesSearch(entry.name, remoteSearch_))
                continue;
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0F);
            ImGui::TableNextColumn();
            const bool selected{selectedRemote_ == entry.path};
            if (px::ui::SelectableRow({entry.path}, entry.name, selected, ImGuiSelectableFlags_SpanAllColumns, {0.0F, 30.0F})) {
                selectedRemote_ = entry.path;
                selectedRemoteDirectory_ = entry.directory;
                if (entry.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    static_cast<void>(session_->ListRemoteDirectory(entry.path));
            }
            const std::string contextId{"remote-entry-context##" + entry.path};
            const px::ui::ContextMenuScope context{{contextId}};
            if (context.Open()) {
                selectedRemote_ = entry.path;
                selectedRemoteDirectory_ = entry.directory;
                if (px::ui::MenuAction({"rename-remote-entry"}, text(ClientText::Rename), {.icon = px::ui::VectorIcon::Pencil}))
                    BeginOperation(FileOperation::RenameRemote, entry.name);
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", FormatModified(entry.modifiedTime).c_str());
            ImGui::TableNextColumn();
            if (!entry.directory)
                ImGui::TextDisabled("%s", FormatSize(entry.size).c_str());
        }
        ImGui::EndTable();
    }
    const bool connected{session_->Snapshot().state == ClientConnectionState::Connected};
    if (px::ui::ActionButton({"receive-selected"}, text(ClientText::Receive),
                             {.variant = px::ui::ButtonVariant::Outline,
                              .icon = px::ui::VectorIcon::FileTransfer,
                              .width = -1.0F,
                              .disabled = selectedRemote_.empty() || !connected}))
        static_cast<void>(session_->StartDownload(selectedRemote_, localFiles_.Path()));
}

void ClientFileTransferWindow::DrawTransferQueue() {
    const auto text = [this](const ClientText id) { return ClientTextValue(id, english_).data(); };
    const float height{ImGui::GetContentRegionAvail().y};
    px::ui::CardScope card{{"file-transfer-queue"}, {0.0F, height}};
    if (!card.Visible())
        return;
    px::ui::SectionTitle(text(ClientText::TransferQueue));
    const auto jobs = session_->TransferJobs();
    if (jobs.empty()) {
        px::ui::EmptyState(px::ui::VectorIcon::FileTransfer, text(ClientText::NoTransfers), {});
        return;
    }
    for (const auto& job : jobs) {
        ImGui::PushID(job.id);
        ImGui::Text("#%d  %s", job.id, text(job.download ? ClientText::Download : ClientText::Upload));
        const float progress{
            job.totalBytes == 0U ? 0.0F : std::clamp(static_cast<float>(job.completedBytes) / static_cast<float>(job.totalBytes), 0.0F, 1.0F)};
        px::ui::Progress(progress, -1.0F);
        ImGui::TextDisabled("%s / %s   %.1f KB/s", FormatSize(job.completedBytes).c_str(), FormatSize(job.totalBytes).c_str(),
                            job.bytesPerSecond / 1024.0);
        if (!job.done && px::ui::ActionButton({"cancel-queue-job"}, text(ClientText::Cancel),
                                              {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Xs}))
            static_cast<void>(session_->CancelTransfer(job.id));
        if (!job.error.empty())
            px::ui::FieldError(job.error);
        ImGui::Separator();
        ImGui::PopID();
    }
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
    const auto text = [this](const ClientText id) { return ClientTextValue(id, english_).data(); };
    static_cast<void>(
        px::ui::DialogHeader({"standalone-file-error-close"}, text(ClientText::ConnectionFailed), snapshot.status,
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
    const auto text = [this](const ClientText id) { return ClientTextValue(id, english_).data(); };
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
        accepted = localFiles_.Rename(selectedLocal_, operationValue_);
        break;
    case FileOperation::RenameRemote:
        accepted = session_->RenameRemoteEntry(selectedRemote_, operationValue_);
        break;
    case FileOperation::DeleteLocal:
        accepted = localFiles_.Remove(selectedLocal_);
        break;
    case FileOperation::DeleteRemote:
        accepted = session_->RemoveRemoteEntry(selectedRemote_, selectedRemoteDirectory_);
        break;
    case FileOperation::None:
        break;
    }
    if (!accepted) {
        operationError_ =
            operation_ == FileOperation::CreateLocal || operation_ == FileOperation::RenameLocal || operation_ == FileOperation::DeleteLocal
                ? localFiles_.Error()
                : "The remote operation could not be queued.";
        return;
    }
    if (operation_ == FileOperation::RenameLocal || operation_ == FileOperation::DeleteLocal)
        selectedLocal_.clear();
    operation_ = FileOperation::None;
    ImGui::CloseCurrentPopup();
}

} // namespace px::client::imgui
