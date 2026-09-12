#include "security_records_page.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <string>
#include <utility>

namespace px::panel::ui {

SecurityRecordsPage::SecurityRecordsPage(std::shared_ptr<SecurityRecordsPort> port) : port_{std::move(port)} {}

void SecurityRecordsPage::Draw(const px::ui::Localizer& localizer) {
    if (ImGui::Selectable(localizer.Text(px::ui::TextId::VisitHistory).data(), selected_ == SecurityRecordKind::Visit, 0, ImVec2{160.0F, 0.0F})) {
        selected_ = SecurityRecordKind::Visit;
    }
    ImGui::SameLine();
    if (ImGui::Selectable(localizer.Text(px::ui::TextId::FileTransferHistory).data(), selected_ == SecurityRecordKind::FileTransfer, 0,
                          ImVec2{180.0F, 0.0F})) {
        selected_ = SecurityRecordKind::FileTransfer;
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::ClearAll).data())) {
        deleteAll_ = true;
        pendingDeleteId_ = 0;
        openDeleteDialog_ = true;
    }
    ImGui::Separator();
    DrawRecords(localizer);
    DrawDeleteDialog(localizer);
}

void SecurityRecordsPage::DrawRecords(const px::ui::Localizer& localizer) {
    const auto records = port_->Snapshot(selected_);
    if (records.empty()) {
        ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::NoSecurityRecords).data());
        return;
    }
    const bool visits{selected_ == SecurityRecordKind::Visit};
    const int columns{visits ? 7 : 8};
    if (!ImGui::BeginTable("SecurityRecords", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollX)) {
        return;
    }
    ImGui::TableSetupColumn(localizer.Text(visits ? px::ui::TextId::ConnectionType : px::ui::TextId::Result).data());
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::StartTime).data());
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::EndTime).data());
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::VisitorDevice).data());
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::TargetDevice).data());
    if (visits) {
        ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::Duration).data());
    } else {
        ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::Direction).data());
        ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::FileName).data());
    }
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::Actions).data());
    ImGui::TableHeadersRow();
    for (const auto& record : records) {
        ImGui::PushID(record.id);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted((visits ? record.type : (record.succeeded ? "OK" : "Failed")).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.startedAt.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.endedAt.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.visitor.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.target.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted((visits ? record.duration : record.direction).c_str());
        if (!visits) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(record.fileName.c_str());
        }
        ImGui::TableNextColumn();
        if (ImGui::SmallButton(localizer.Text(px::ui::TextId::Copy).data())) {
            SDL_SetClipboardText(record.plainText.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(localizer.Text(px::ui::TextId::CopyJson).data())) {
            SDL_SetClipboardText(record.json.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(localizer.Text(px::ui::TextId::Delete).data())) {
            deleteAll_ = false;
            pendingDeleteId_ = record.id;
            openDeleteDialog_ = true;
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void SecurityRecordsPage::DrawDeleteDialog(const px::ui::Localizer& localizer) {
    if (openDeleteDialog_) {
        password_.fill({});
        passwordRejected_ = false;
        ImGui::OpenPopup("DeleteSecurityRecords");
        openDeleteDialog_ = false;
    }
    if (!ImGui::BeginPopupModal("DeleteSecurityRecords", {}, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::EnterLongTermPassword).data());
    ImGui::InputText("##RecordPassword", password_.data(), password_.size(), ImGuiInputTextFlags_Password);
    if (passwordRejected_) {
        ImGui::TextColored(ImVec4{0.9F, 0.28F, 0.25F, 1.0F}, "%s", localizer.Text(px::ui::TextId::PasswordInvalid).data());
    }
    if (ImGui::Button(localizer.Text(px::ui::TextId::Delete).data())) {
        passwordRejected_ = deleteAll_ ? !port_->DeleteAll(selected_, password_.data())
                                       : !port_->Delete(selected_, pendingDeleteId_, password_.data());
        if (!passwordRejected_) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace px::panel::ui
