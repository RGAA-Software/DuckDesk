#include "security_records_page.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <string>
#include <utility>

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/navigation.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

namespace px::panel::ui {

SecurityRecordsPage::SecurityRecordsPage(std::shared_ptr<SecurityRecordsPort> port) : port_{std::move(port)} {}

void SecurityRecordsPage::Draw(const px::ui::Localizer& localizer) {
    px::ui::PageTitle(localizer.Text(px::ui::TextId::Security));
    DrawContent(localizer);
}

void SecurityRecordsPage::DrawEmbedded(const px::ui::Localizer& localizer) { DrawContent(localizer); }

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
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - px::ui::Scale(96.0F));
    if (px::ui::ActionButton({"security-clear-all"}, localizer.Text(px::ui::TextId::ClearAll),
                             {.variant = px::ui::ButtonVariant::Destructive, .width = px::ui::Scale(96.0F)})) {
        deleteAll_ = true;
        pendingDeleteId_ = 0;
        openDeleteDialog_ = true;
    }
    px::ui::HorizontalSeparator();
    ImGui::Spacing();
    DrawRecords(localizer);
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
    const px::ui::UiMetrics metrics{px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const px::ui::ScopedStyleVar cellPadding{ImGuiStyleVar_CellPadding, ImVec2{metrics.spacingSm, metrics.spacingXs}};
    const ImGuiTableFlags tableFlags{ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                     ImGuiTableFlags_NoBordersInBodyUntilResize | ImGuiTableFlags_SizingStretchProp};
    if (!ImGui::BeginTable(visits ? "SecurityVisits" : "SecurityTransfers", columns, tableFlags, {0.0F, ImGui::GetContentRegionAvail().y})) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(localizer.Text(visits ? px::ui::TextId::ConnectionType : px::ui::TextId::Result).data(),
                            ImGuiTableColumnFlags_WidthStretch, 0.6F);
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::StartTime).data(), ImGuiTableColumnFlags_WidthStretch, 1.3F);
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::EndTime).data(), ImGuiTableColumnFlags_WidthStretch, 1.3F);
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::VisitorDevice).data(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::TargetDevice).data(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
    if (visits) {
        ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::Duration).data(), ImGuiTableColumnFlags_WidthStretch, 0.7F);
    } else {
        ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::Direction).data(), ImGuiTableColumnFlags_WidthStretch, 0.7F);
        ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::FileName).data(), ImGuiTableColumnFlags_WidthStretch, 1.2F);
    }
    ImGui::TableSetupColumn(localizer.Text(px::ui::TextId::Actions).data(), ImGuiTableColumnFlags_WidthFixed,
                            metrics.controlSm * 3.0F + metrics.spacingXs * 2.0F);
    {
        const float headerPadding{(metrics.tableRowHeight - ImGui::GetTextLineHeight()) * 0.5F};
        const px::ui::ScopedStyleVar headerCellPadding{ImGuiStyleVar_CellPadding, ImVec2{metrics.spacingSm, headerPadding}};
        ImGui::TableHeadersRow();
    }
    const float contentHeight{metrics.tableRowHeight - metrics.spacingXs * 2.0F};
    for (const auto& record : records) {
        ImGui::PushID(record.id);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, metrics.tableRowHeight);
        ImGui::TableNextColumn();
        px::ui::ClippedText(
            visits ? std::string_view{record.type} : localizer.Text(record.succeeded ? px::ui::TextId::Succeeded : px::ui::TextId::OperationFailed),
            0.0F, contentHeight);
        ImGui::TableNextColumn();
        px::ui::ClippedText(record.startedAt, 0.0F, contentHeight);
        ImGui::TableNextColumn();
        px::ui::ClippedText(record.endedAt, 0.0F, contentHeight);
        ImGui::TableNextColumn();
        px::ui::ClippedText(record.visitor, 0.0F, contentHeight);
        ImGui::TableNextColumn();
        px::ui::ClippedText(record.target, 0.0F, contentHeight);
        ImGui::TableNextColumn();
        px::ui::ClippedText(visits ? record.duration : record.direction, 0.0F, contentHeight);
        if (!visits) {
            ImGui::TableNextColumn();
            px::ui::ClippedText(record.fileName, 0.0F, contentHeight);
        }
        ImGui::TableNextColumn();
        if (px::ui::IconAction({"record-copy"}, px::ui::VectorIcon::Copy, localizer.Text(px::ui::TextId::Copy),
                               {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconSm})) {
            SDL_SetClipboardText(record.plainText.c_str());
        }
        ImGui::SameLine(0.0F, metrics.spacingXs);
        if (px::ui::IconAction({"record-copy-json"}, px::ui::VectorIcon::File, localizer.Text(px::ui::TextId::CopyJson),
                               {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconSm})) {
            SDL_SetClipboardText(record.json.c_str());
        }
        ImGui::SameLine(0.0F, metrics.spacingXs);
        if (px::ui::IconAction({"record-delete"}, px::ui::VectorIcon::Trash, localizer.Text(px::ui::TextId::Delete),
                               {.variant = px::ui::ButtonVariant::GhostDestructive, .size = px::ui::WidgetSize::IconSm})) {
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

}  // namespace px::panel::ui
