#include "cloud_applications_page.h"

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
#include <string>
#include <string_view>
#include <utility>

namespace px::panel::ui {
namespace {

std::size_t NextUtf8Boundary(const std::string_view value, const std::size_t offset) noexcept {
    if (offset >= value.size())
        return value.size();
    const unsigned char lead{static_cast<unsigned char>(value[offset])};
    const std::size_t length{lead < 0x80U ? 1U : lead < 0xE0U ? 2U : lead < 0xF0U ? 3U : 4U};
    return std::min(value.size(), offset + length);
}

std::size_t FittingEnd(const std::string_view value, const std::size_t begin, const float width, const std::string_view suffix = {}) {
    std::size_t fitting{begin};
    for (std::size_t end{NextUtf8Boundary(value, begin)}; end <= value.size();) {
        const std::string candidate{std::string{value.substr(begin, end - begin)} + std::string{suffix}};
        if (ImGui::CalcTextSize(candidate.c_str()).x > width)
            break;
        fitting = end;
        if (end == value.size())
            break;
        end = NextUtf8Boundary(value, end);
    }
    return fitting;
}

std::string SingleLineLabel(const std::string& value, const float width) {
    const std::string_view view{value};
    constexpr std::string_view ellipsis{"..."};
    if (ImGui::CalcTextSize(value.c_str()).x <= width)
        return value;
    const std::size_t end{FittingEnd(view, 0, width, ellipsis)};
    return std::string{view.substr(0, end)} + std::string{ellipsis};
}

px::ui::VectorIcon ApplicationIcon(const CloudApplicationKind kind) noexcept {
    switch (kind) {
    case CloudApplicationKind::Remote:
        return px::ui::VectorIcon::Monitor;
    case CloudApplicationKind::Game:
        return px::ui::VectorIcon::Gamepad;
    case CloudApplicationKind::WebView:
        return px::ui::VectorIcon::Globe;
    case CloudApplicationKind::Rdp:
        return px::ui::VectorIcon::Panels;
    }
    return px::ui::VectorIcon::Monitor;
}

} // namespace

CloudApplicationsPage::CloudApplicationsPage(std::shared_ptr<CloudApplicationsPort> port) : port_{std::move(port)} {}

void CloudApplicationsPage::Draw(const px::ui::Localizer& localizer) {
    if (const auto request = port_->PendingPasswordRequest(); request && passwordStreamId_.empty()) {
        passwordStreamId_ = request->streamId;
        password_.clear();
        passwordDialogOpen_ = true;
    }
    DrawPasswordDialog(localizer);
    px::ui::PageTitle(localizer.Text(px::ui::TextId::CloudApplications));
    ImGui::SameLine(0.0F, px::ui::Scale(14.0F));
    if (px::ui::ActionButton(
            {"cloud-refresh"}, localizer.Text(px::ui::TextId::Refresh),
            {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Xs, .icon = px::ui::VectorIcon::Refresh, .circular = true})) {
        port_->Refresh();
    }
    const auto applications = port_->Snapshot();
    if (applications.empty()) {
        px::ui::EmptyState(px::ui::VectorIcon::Cloud, localizer.Text(px::ui::TextId::NoCloudApplications), {});
        return;
    }
    constexpr std::size_t cardsPerRow{3};
    const float spacing{ImGui::GetStyle().ItemSpacing.x};
    const float availableWidth{ImGui::GetContentRegionAvail().x};
    const float fittingWidth{(availableWidth - spacing * static_cast<float>(cardsPerRow - 1)) / static_cast<float>(cardsPerRow)};
    const float cardWidth{std::min(px::ui::Scale(236.0F), fittingWidth)};
    for (std::size_t index{}; index < applications.size(); ++index) {
        if (index % cardsPerRow != 0)
            ImGui::SameLine();
        DrawApplicationCard(applications[index], localizer, index, cardWidth);
    }
}

void CloudApplicationsPage::DrawApplicationCard(const CloudApplicationCard& application, const px::ui::Localizer& localizer, const std::size_t index,
                                                const float width) {
    const ImVec2 cardSize{width, px::ui::Scale(60.0F)};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    const ImVec2 maximum{minimum.x + cardSize.x, minimum.y + cardSize.y};
    const std::string id{"cloud-application-" + std::to_string(index) + "-" + application.streamId};
    const bool running{application.instanceState == "running"};
    const bool busy{application.instanceState == "starting" || application.instanceState == "stopping"};
    ImGui::PushID(id.c_str());
    ImGui::InvisibleButton("##card", cardSize);
    const bool hovered{ImGui::IsItemHovered()};
    auto& draw = *ImGui::GetWindowDrawList();
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    draw.AddRectFilled(minimum, maximum, ImGui::GetColorU32(hovered ? tokens.accent : tokens.card), px::ui::Scale(9.0F));
    draw.AddRect(minimum, maximum, ImGui::GetColorU32(hovered ? tokens.ring : tokens.border), px::ui::Scale(9.0F));
    const ImVec2 iconMinimum{minimum.x + px::ui::Scale(14.0F), minimum.y + px::ui::Scale(14.0F)};
    const float iconTileSize{px::ui::Scale(36.0F)};
    draw.AddRectFilled(iconMinimum, {iconMinimum.x + iconTileSize, iconMinimum.y + iconTileSize}, ImGui::GetColorU32(tokens.secondary),
                       px::ui::Scale(8.0F));
    px::ui::DrawVectorIcon(ApplicationIcon(application.kind), {iconMinimum.x + px::ui::Scale(8.0F), iconMinimum.y + px::ui::Scale(8.0F)},
                           px::ui::Scale(20.0F), ImGui::GetColorU32(tokens.primary));
    const ImU32 statusColor{ImGui::GetColorU32(running ? tokens.success : busy ? tokens.warning : tokens.mutedForeground)};
    draw.AddCircleFilled({maximum.x - px::ui::Scale(14.0F), minimum.y + px::ui::Scale(14.0F)}, px::ui::Scale(3.0F), statusColor);
    const float textLeft{iconMinimum.x + iconTileSize + px::ui::Scale(12.0F)};
    const float textRight{maximum.x - px::ui::Scale(26.0F)};
    const auto label{SingleLineLabel(application.name, textRight - textLeft)};
    draw.PushClipRect({textLeft, minimum.y}, {textRight, maximum.y}, true);
    draw.AddText({textLeft, minimum.y + px::ui::Scale(8.0F)}, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    draw.PopClipRect();
    const std::string state{localizer.Text(running ? px::ui::TextId::Running : px::ui::TextId::Stopped)};
    const ImVec2 stateSize{ImGui::CalcTextSize(state.c_str())};
    const ImVec2 badgeMin{textLeft, maximum.y - px::ui::Scale(25.0F)};
    const ImVec2 badgeMax{badgeMin.x + stateSize.x + px::ui::Scale(14.0F), badgeMin.y + stateSize.y + px::ui::Scale(4.0F)};
    draw.AddRectFilled(badgeMin, badgeMax, ImGui::GetColorU32(tokens.secondary), (badgeMax.y - badgeMin.y) * 0.5F);
    draw.AddText({badgeMin.x + px::ui::Scale(7.0F), badgeMin.y + px::ui::Scale(2.0F)}, statusColor, state.c_str());
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !busy)
        port_->Start(application.streamId, false);
    {
        px::ui::ContextMenuScope menu{{"ApplicationActions"}};
        if (menu.Open()) {
            DrawContextMenu(application, localizer);
        }
    }
    ImGui::PopID();
}

void CloudApplicationsPage::DrawContextMenu(const CloudApplicationCard& application, const px::ui::Localizer& localizer) {
    const bool running{application.instanceState == "running"};
    const bool busy{application.instanceState == "starting" || application.instanceState == "stopping"};
    if (px::ui::MenuAction({"application-start"}, localizer.Text(px::ui::TextId::StartApplication),
                           {.icon = px::ui::VectorIcon::Play, .enabled = !busy}))
        port_->Start(application.streamId, false);
    if (px::ui::MenuAction({"application-view"}, localizer.Text(px::ui::TextId::ViewOnly), {.icon = px::ui::VectorIcon::Eye, .enabled = !busy}))
        port_->Start(application.streamId, true);
    if (px::ui::MenuAction({"application-stop"}, localizer.Text(px::ui::TextId::StopApplication),
                           {.icon = px::ui::VectorIcon::Stop, .variant = px::ui::MenuItemVariant::Destructive, .enabled = running || busy}))
        port_->Stop(application.streamId);
    px::ui::MenuSeparator();
    if (px::ui::MenuAction({"application-force-tcp"}, localizer.Text(px::ui::TextId::ForceTcp),
                           {.icon = px::ui::VectorIcon::Connect, .enabled = !application.rdpMode, .selected = application.forceTcp}))
        port_->SetForceTcp(application.streamId, !application.forceTcp);
    if (px::ui::MenuAction({"application-force-relay"}, localizer.Text(px::ui::TextId::ForceRelay),
                           {.icon = px::ui::VectorIcon::Cloud, .enabled = !application.rdpMode, .selected = application.forceRelay}))
        port_->SetForceRelay(application.streamId, !application.forceRelay);
}

void CloudApplicationsPage::DrawPasswordDialog(const px::ui::Localizer& localizer) {
    if (passwordDialogOpen_) {
        px::ui::OpenModal({"CloudApplicationPassword"});
        passwordDialogOpen_ = false;
    }
    px::ui::ModalScope dialog{{"CloudApplicationPassword"}, 420.0F};
    if (!dialog.Open())
        return;
    static_cast<void>(px::ui::DialogHeader({"close-cloud-password"}, localizer.Text(px::ui::TextId::Password), {},
                                           {.icon = px::ui::VectorIcon::Shield, .closeable = false}));
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::Password));
    static_cast<void>(px::ui::PasswordField({"cloud-password"}, password_));
    const float buttonWidth{px::ui::Scale(96.0F)};
    px::ui::DialogFooter(buttonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
    if (px::ui::ActionButton({"cloud-cancel"}, localizer.Text(px::ui::TextId::Cancel),
                             {.variant = px::ui::ButtonVariant::Outline, .width = buttonWidth})) {
        port_->CancelPassword(passwordStreamId_);
        passwordStreamId_.clear();
        password_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"cloud-connect"}, localizer.Text(px::ui::TextId::Connect),
                             {.icon = px::ui::VectorIcon::Connect, .width = buttonWidth, .disabled = password_.empty()})) {
        port_->SubmitPassword(passwordStreamId_, std::move(password_));
        passwordStreamId_.clear();
        password_.clear();
        ImGui::CloseCurrentPopup();
    }
}

} // namespace px::panel::ui
