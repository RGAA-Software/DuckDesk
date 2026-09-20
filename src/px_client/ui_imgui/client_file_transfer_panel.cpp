#include "client_file_transfer_panel.h"

#include "client_file_transfer_format.h"
#include "client_session.h"
#include "client_text.h"
#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <algorithm>

namespace px::client::imgui {

void ClientFileTransferPanel::Open() {
    open_ = true;
}

bool ClientFileTransferPanel::CapturesPointer(const float x, const float y) const noexcept {
    return open_ && x >= windowX_ && y >= windowY_ && x < windowX_ + windowWidth_ && y < windowY_ + windowHeight_;
}

bool ClientFileTransferPanel::CapturesKeyboard() const noexcept {
    return open_ && capturesKeyboard_;
}

void ClientFileTransferPanel::Draw(const std::shared_ptr<ClientSession>& session, const bool english) {
    if (!open_ || !session) {
        capturesKeyboard_ = false;
        return;
    }
    const auto text = [english](const ClientText id) { return ClientTextValue(id, english).data(); };
    ImGui::SetNextWindowSize({980.0F, 650.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(text(ClientText::FileTransfer), &open_)) {
        const ImVec2 position{ImGui::GetWindowPos()};
        const ImVec2 size{ImGui::GetWindowSize()};
        windowX_ = position.x;
        windowY_ = position.y;
        windowWidth_ = size.x;
        windowHeight_ = size.y;
        capturesKeyboard_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::End();
        return;
    }

    const ImVec2 position{ImGui::GetWindowPos()};
    const ImVec2 size{ImGui::GetWindowSize()};
    windowX_ = position.x;
    windowY_ = position.y;
    windowWidth_ = size.x;
    windowHeight_ = size.y;
    capturesKeyboard_ =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && (ImGui::GetIO().WantCaptureKeyboard || ImGui::GetIO().WantTextInput);

    px::ui::PageTitle(text(ClientText::FileTransfer));
    ImGui::Spacing();
    {
        px::ui::CardScope paths{{"client-transfer-paths"}, {0.0F, 142.0F}};
        if (paths.Visible() && ImGui::BeginTable("client-transfer-path-table", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 110.0F);
            ImGui::TableSetupColumn("path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, 180.0F);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, 48.0F);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            px::ui::MutedText(text(ClientText::RemotePath));
            ImGui::TableNextColumn();
            static_cast<void>(px::ui::TextField({"remote-path"}, remotePath_));
            ImGui::TableNextColumn();
            if (px::ui::ActionButton({"open-remote-path"}, text(ClientText::Open),
                                     {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Sm, .width = -1.0F})) {
                static_cast<void>(session->ListRemoteDirectory(remotePath_));
            }

            ImGui::TableNextRow(ImGuiTableRowFlags_None, 48.0F);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            px::ui::MutedText(text(ClientText::LocalPath));
            ImGui::TableNextColumn();
            static_cast<void>(px::ui::TextField({"local-path"}, localPath_));
            ImGui::TableNextColumn();
            if (px::ui::ActionButton({"upload-local-path"}, text(ClientText::UploadLocalPath),
                                     {.size = px::ui::WidgetSize::Sm, .icon = px::ui::VectorIcon::FileTransfer, .width = -1.0F})) {
                static_cast<void>(session->StartUpload(localPath_, remotePath_));
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    {
        px::ui::CardScope files{{"client-remote-files"}, {0.0F, 338.0F}};
        if (files.Visible()) {
            px::ui::SectionTitle(text(ClientText::RemoteFiles));
            px::ui::HorizontalSeparator();
            if (ImGui::BeginTable("remote-files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                                  {0.0F, 242.0F})) {
                ImGui::TableSetupColumn(text(ClientText::Name));
                ImGui::TableSetupColumn(text(ClientText::Type), ImGuiTableColumnFlags_WidthFixed, 100.0F);
                ImGui::TableSetupColumn(text(ClientText::Size), ImGuiTableColumnFlags_WidthFixed, 150.0F);
                ImGui::TableHeadersRow();
                for (const auto& entry : session->RemoteEntries()) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const bool selected = selectedRemote_ == entry.path;
                    if (px::ui::SelectableRow({entry.path}, entry.name, selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedRemote_ = entry.path;
                        if (entry.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            remotePath_ = entry.path;
                            static_cast<void>(session->ListRemoteDirectory(entry.path));
                        }
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(text(entry.directory ? ClientText::Folder : ClientText::File));
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", static_cast<unsigned long long>(entry.size));
                }
                ImGui::EndTable();
            }
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 180.0F);
            if (px::ui::ActionButton({"download-selection"}, text(ClientText::DownloadSelection),
                                     {.variant = px::ui::ButtonVariant::Outline,
                                      .size = px::ui::WidgetSize::Sm,
                                      .icon = px::ui::VectorIcon::FileTransfer,
                                      .width = 180.0F,
                                      .disabled = selectedRemote_.empty()})) {
                static_cast<void>(session->StartDownload(selectedRemote_, localPath_));
            }
        }
    }

    ImGui::Spacing();
    px::ui::SectionTitle(text(ClientText::Transfers));
    for (const auto& job : session->TransferJobs()) {
        ImGui::PushID(job.id);
        const float progress = job.totalBytes == 0 ? 0.0F : std::clamp(static_cast<float>(job.completedBytes) / job.totalBytes, 0.0F, 1.0F);
        ImGui::Text("#%d %s", job.id, text(job.download ? ClientText::Download : ClientText::Upload));
        ImGui::SameLine();
        px::ui::Progress(progress, 300.0F);
        ImGui::SameLine();
        const std::string speed{FormatTransferSpeed(job.bytesPerSecond)};
        ImGui::TextUnformatted(speed.c_str());
        if (!job.error.empty()) {
            ImGui::SameLine();
            if (px::ui::ActionButton({"resume-transfer"}, text(ClientText::Resume),
                                     {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Xs}))
                static_cast<void>(session->ResumeTransfer(job.id));
        } else if (!job.done) {
            ImGui::SameLine();
            if (px::ui::ActionButton({"cancel-transfer"}, text(ClientText::Cancel),
                                     {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Xs}))
                static_cast<void>(session->CancelTransfer(job.id));
        }
        if (!job.error.empty())
            px::ui::FieldError(job.error);
        ImGui::PopID();
    }

    if (const auto overwrite = session->PendingOverwrite()) {
        px::ui::OpenModal({"file-overwrite"});
        const px::ui::ModalScope modal{{"file-overwrite"}, 520.0F};
        if (modal.Open()) {
            static_cast<void>(
                px::ui::DialogHeader({"file-overwrite-close"}, text(ClientText::DestinationExists), overwrite->path,
                                     {.icon = px::ui::VectorIcon::TriangleAlert, .tone = px::ui::BadgeVariant::Warning, .closeable = false}));
            static_cast<void>(px::ui::CheckboxField({"overwrite-apply-all"}, text(ClientText::ApplyToAll), applyOverwriteToAll_));
            constexpr float buttonWidth{130.0F};
            px::ui::DialogFooter(buttonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
            if (px::ui::ActionButton({"overwrite-skip"}, text(ClientText::Skip), {.variant = px::ui::ButtonVariant::Outline, .width = buttonWidth})) {
                static_cast<void>(session->ConfirmOverwrite(false, applyOverwriteToAll_));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (px::ui::ActionButton({"overwrite-confirm"}, text(ClientText::Overwrite), {.width = buttonWidth})) {
                static_cast<void>(session->ConfirmOverwrite(true, applyOverwriteToAll_));
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::End();
}

} // namespace px::client::imgui
