#include "security_records_page.h"

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/navigation.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <string>
#include <utility>

namespace px::panel::ui {

SecurityRecordsPage::SecurityRecordsPage(std::shared_ptr<SecurityRecordsPort> port) : port_{std::move(port)} {}

void SecurityRecordsPage::Draw(const px::ui::Localizer& localizer) {
    px::ui::PageTitle(localizer.Text(px::ui::TextId::Security));
    DrawContent(localizer);
}

void SecurityRecordsPage::DrawEmbedded(const px::ui::Localizer& localizer) {
    DrawContent(localizer);
}

void SecurityRecordsPage::DrawContent(const px::ui::Localizer& localizer) {
    if (px::ui::TabItem({"security-visits"}, localizer.Text(px::ui::TextId::VisitHistory), selected_ == SecurityRecordKind::Visit,
                        px::ui::Scale(84.0F))) {
        selected_ = SecurityRecordKind::Visit;
    }
    ImGui::SameLine();
    if (px::ui::TabItem({"security-files"}, localizer.Text(px::ui::TextId::FileTransferHistory), selected_ == SecurityRecordKind::FileTransfer,
                        px::ui::Scale(100.0F))) {
        selected_ = SecurityRecordKind::FileTransfer;
    }
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - px::ui::Scale(88.0F));
    if (px::ui::ActionButton({"security-clear-all"}, localizer.Text(px::ui::TextId::ClearAll),
                             {.variant = px::ui::ButtonVariant::Destructive, .size = px::ui::WidgetSize::Sm, .width = px::ui::Scale(88.0F)})) {
        deleteAll_ = true;
        pendingDeleteId_ = 0;
        openDeleteDialog_ = true;
    }
    px::ui::HorizontalSeparator();
    ImGui::Spacing();
    {
        px::ui::CardScope records{{"SecurityRecordsCard"}, {0.0F, ImGui::GetContentRegionAvail().y}};
        if (records.Visible()) {
            DrawRecords(localizer);
        }
    }
    DrawDeleteDialog(localizer);
}

void SecurityRecordsPage::DrawRecords(const px::ui::Localizer& localizer) {
    const auto records = port_->Snapshot(selected_);
    if (records.empty()) {
        px::ui::EmptyState(px::ui::VectorIcon::Shield, localizer.Text(px::ui::TextId::NoSecurityRecords), {});
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
        if (px::ui::IconAction({"record-copy"}, px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy),
                               {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconXs})) {
            SDL_SetClipboardText(record.plainText.c_str());
        }
        ImGui::SameLine();
        if (px::ui::ActionButton({"record-copy-json"}, localizer.Text(px::ui::TextId::CopyJson),
                                 {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Xs})) {
            SDL_SetClipboardText(record.json.c_str());
        }
        ImGui::SameLine();
        if (px::ui::IconAction({"record-delete"}, px::ui::VectorIcon::Trash, localizer.Text(px::ui::TextId::Delete),
                               {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconXs})) {
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
        password_.clear();
        passwordRejected_ = false;
        px::ui::OpenModal({"DeleteSecurityRecords"});
        openDeleteDialog_ = false;
    }
    px::ui::ModalScope dialog{{"DeleteSecurityRecords"}, 440.0F};
    if (!dialog.Open()) {
        return;
    }
    static_cast<void>(px::ui::DialogHeader({"record-delete-close"}, localizer.Text(px::ui::TextId::Delete),
                                           localizer.Text(px::ui::TextId::EnterLongTermPassword),
                                           {.icon = px::ui::VectorIcon::Trash, .tone = px::ui::BadgeVariant::Destructive, .closeable = false}));
    static_cast<void>(px::ui::PasswordField({"record-password"}, password_, {}, {.invalid = passwordRejected_}));
    if (passwordRejected_) {
        px::ui::FieldError(localizer.Text(px::ui::TextId::PasswordInvalid));
    }
    const float buttonWidth{px::ui::Scale(104.0F)};
    px::ui::DialogFooter(buttonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
    if (px::ui::ActionButton({"record-delete-cancel"}, localizer.Text(px::ui::TextId::Cancel),
                             {.variant = px::ui::ButtonVariant::Outline, .width = buttonWidth})) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"record-delete-confirm"}, localizer.Text(px::ui::TextId::Delete),
                             {.variant = px::ui::ButtonVariant::Destructive, .width = buttonWidth, .disabled = password_.empty()})) {
        passwordRejected_ = deleteAll_ ? !port_->DeleteAll(selected_, password_) : !port_->Delete(selected_, pendingDeleteId_, password_);
        if (!passwordRejected_) {
            ImGui::CloseCurrentPopup();
        }
    }
}

} // namespace px::panel::ui
