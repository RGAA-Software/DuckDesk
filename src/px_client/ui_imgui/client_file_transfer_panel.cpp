#include "client_file_transfer_panel.h"

#include "client_session.h"
#include "client_text.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace px::client::imgui {

void ClientFileTransferPanel::Open() {
    open_ = true;
}

void ClientFileTransferPanel::Draw(const std::shared_ptr<ClientSession>& session, const bool english) {
    if (!open_ || !session) return;
    const auto text = [english](const ClientText id) { return ClientTextValue(id, english).data(); };
    ImGui::SetNextWindowSize({980.0F, 650.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(text(ClientText::FileTransfer), &open_)) {
        ImGui::End();
        return;
    }

    ImGui::InputText(text(ClientText::RemotePath), remotePath_.data(), remotePath_.size());
    ImGui::SameLine();
    if (ImGui::Button(text(ClientText::Open))) static_cast<void>(session->ListRemoteDirectory(remotePath_.data()));
    ImGui::InputText(text(ClientText::LocalPath), localPath_.data(), localPath_.size());
    if (ImGui::Button(text(ClientText::UploadLocalPath))) {
        static_cast<void>(session->StartUpload(localPath_.data(), remotePath_.data()));
    }
    ImGui::SameLine();
    if (ImGui::Button(text(ClientText::DownloadSelection))) {
        static_cast<void>(session->StartDownload(selectedRemote_, localPath_.data()));
    }

    ImGui::SeparatorText(text(ClientText::RemoteFiles));
    if (ImGui::BeginTable("remote-files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                          {0.0F, 300.0F})) {
        ImGui::TableSetupColumn(text(ClientText::Name));
        ImGui::TableSetupColumn(text(ClientText::Type), ImGuiTableColumnFlags_WidthFixed, 100.0F);
        ImGui::TableSetupColumn(text(ClientText::Size), ImGuiTableColumnFlags_WidthFixed, 150.0F);
        ImGui::TableHeadersRow();
        for (const auto& entry : session->RemoteEntries()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool selected = selectedRemote_ == entry.path;
            if (ImGui::Selectable(entry.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                selectedRemote_ = entry.path;
                if (entry.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    std::snprintf(remotePath_.data(), remotePath_.size(), "%s", entry.path.c_str());
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

    ImGui::SeparatorText(text(ClientText::Transfers));
    for (const auto& job : session->TransferJobs()) {
        ImGui::PushID(job.id);
        const float progress = job.totalBytes == 0 ? 0.0F : std::clamp(static_cast<float>(job.completedBytes) / job.totalBytes, 0.0F, 1.0F);
        ImGui::Text("#%d %s", job.id, text(job.download ? ClientText::Download : ClientText::Upload));
        ImGui::SameLine();
        ImGui::ProgressBar(progress, {300.0F, 0.0F});
        ImGui::SameLine();
        ImGui::Text("%.1f KB/s", job.bytesPerSecond / 1024.0);
        if (!job.done) {
            ImGui::SameLine();
            if (ImGui::SmallButton(text(ClientText::Cancel))) static_cast<void>(session->CancelTransfer(job.id));
        }
        if (!job.error.empty()) ImGui::TextDisabled("%s", job.error.c_str());
        ImGui::PopID();
    }

    if (const auto overwrite = session->PendingOverwrite()) {
        ImGui::OpenPopup("file-overwrite");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
        if (ImGui::BeginPopupModal("file-overwrite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s\n%s", text(ClientText::DestinationExists), overwrite->path.c_str());
            ImGui::Checkbox(text(ClientText::ApplyToAll), &applyOverwriteToAll_);
            if (ImGui::Button(text(ClientText::Overwrite), {130.0F, 0.0F})) {
                static_cast<void>(session->ConfirmOverwrite(true, applyOverwriteToAll_));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(text(ClientText::Skip), {130.0F, 0.0F})) {
                static_cast<void>(session->ConfirmOverwrite(false, applyOverwriteToAll_));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

} // namespace px::client::imgui
