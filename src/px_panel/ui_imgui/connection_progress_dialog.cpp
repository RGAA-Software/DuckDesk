#include "connection_progress_dialog.h"

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/theme_tokens.h"
#include "px_ui/vector_icon.h"

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace px::panel::ui {
namespace {

px::ui::TextId StepText(const ConnectionStepKind kind) {
    switch (kind) {
    case ConnectionStepKind::ValidateTarget:
        return px::ui::TextId::ConnectionStepValidateTarget;
    case ConnectionStepKind::ResolveDevice:
        return px::ui::TextId::ConnectionStepResolveDevice;
    case ConnectionStepKind::ReachEndpoint:
        return px::ui::TextId::ConnectionStepReachEndpoint;
    case ConnectionStepKind::CheckPermission:
        return px::ui::TextId::ConnectionStepCheckPermission;
    case ConnectionStepKind::VerifyPassword:
        return px::ui::TextId::ConnectionStepVerifyPassword;
    case ConnectionStepKind::LaunchClient:
        return px::ui::TextId::ConnectionStepLaunchClient;
    }
    return px::ui::TextId::OperationFailed;
}

px::ui::TextId FailureText(const ConnectionFailureReason reason) {
    switch (reason) {
    case ConnectionFailureReason::InvalidTarget:
        return px::ui::TextId::ConnectionTargetInvalid;
    case ConnectionFailureReason::ConsoleLoginRequired:
        return px::ui::TextId::ConnectionConsoleLoginRequired;
    case ConnectionFailureReason::DeviceResolutionFailed:
        return px::ui::TextId::ConnectionDeviceResolutionFailed;
    case ConnectionFailureReason::NoUsableAddress:
        return px::ui::TextId::ConnectionNoUsableAddress;
    case ConnectionFailureReason::DeviceUnreachable:
        return px::ui::TextId::ConnectionDeviceUnreachable;
    case ConnectionFailureReason::RemotePreflightUnavailable:
        return px::ui::TextId::ConnectionRemotePreflightUnavailable;
    case ConnectionFailureReason::RemoteAccessDisabled:
        return px::ui::TextId::ConnectionRemoteAccessDisabled;
    case ConnectionFailureReason::FileTransferDisabled:
        return px::ui::TextId::ConnectionFileTransferDisabled;
    case ConnectionFailureReason::RemoteSessionOccupied:
        return px::ui::TextId::ConnectionRemoteSessionOccupied;
    case ConnectionFailureReason::RemoteReconnectGrace:
        return px::ui::TextId::ConnectionRemoteReconnectGrace;
    case ConnectionFailureReason::PasswordRequired:
        return px::ui::TextId::ConnectionPasswordRequired;
    case ConnectionFailureReason::PasswordRejected:
        return px::ui::TextId::ConnectionPasswordRejected;
    case ConnectionFailureReason::PasswordVerificationUnavailable:
        return px::ui::TextId::ConnectionPasswordVerificationUnavailable;
    case ConnectionFailureReason::ClientLaunchFailed:
        return px::ui::TextId::ConnectionClientLaunchFailed;
    case ConnectionFailureReason::WorkerUnavailable:
        return px::ui::TextId::ConnectionWorkerUnavailable;
    case ConnectionFailureReason::None:
    default:
        return px::ui::TextId::OperationFailed;
    }
}

std::string IntentDescription(const px::ui::Localizer& localizer, const ConnectionProgress& progress) {
    px::ui::TextId intentText{px::ui::TextId::RemoteControl};
    if (progress.intent == ConnectionIntent::ViewOnly)
        intentText = px::ui::TextId::ViewOnly;
    else if (progress.intent == ConnectionIntent::FileTransfer)
        intentText = px::ui::TextId::FileTransfer;
    std::string result{localizer.Text(intentText)};
    if (!progress.target.empty()) {
        result += "  ·  ";
        result += progress.target;
    }
    return result;
}

px::ui::BadgeVariant BadgeFor(const ConnectionStepState state) {
    switch (state) {
    case ConnectionStepState::Succeeded:
        return px::ui::BadgeVariant::Success;
    case ConnectionStepState::Failed:
        return px::ui::BadgeVariant::Destructive;
    case ConnectionStepState::Running:
        return px::ui::BadgeVariant::Default;
    case ConnectionStepState::Pending:
    default:
        return px::ui::BadgeVariant::Secondary;
    }
}

std::string_view StatusText(const px::ui::Localizer& localizer, const ConnectionStepState state) {
    switch (state) {
    case ConnectionStepState::Succeeded:
        return localizer.Text(px::ui::TextId::Verified);
    case ConnectionStepState::Failed:
        return localizer.Text(px::ui::TextId::OperationFailed);
    case ConnectionStepState::Running:
        return localizer.Text(px::ui::TextId::Working);
    case ConnectionStepState::Pending:
    default:
        return localizer.Text(px::ui::TextId::ConnectionWaiting);
    }
}

void DrawStep(const px::ui::Localizer& localizer, const ConnectionProgressStep& step, const std::size_t index) {
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    const px::ui::UiMetrics metrics{px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    const float width{ImGui::GetContentRegionAvail().x};
    const float iconSize{metrics.iconDefault};
    const float textLeft{minimum.x + metrics.spacingMd + iconSize + metrics.spacingMd};
    const float detailWidth{width - (textLeft - minimum.x) - metrics.spacingMd};
    const ImVec2 detailSize{step.detail.empty() ? ImVec2{} : ImGui::CalcTextSize(step.detail.c_str(), {}, false, detailWidth)};
    const float rowHeight{step.detail.empty() ? 48.0F * metrics.scale
                                              : std::max(66.0F * metrics.scale,
                                                         metrics.spacingSm * 2.0F + ImGui::GetTextLineHeight() + metrics.spacingXs + detailSize.y)};
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    ImVec4 background{tokens.muted};
    background.w = 0.38F;
    draw.AddRectFilled(minimum, {minimum.x + width, minimum.y + rowHeight}, ImGui::GetColorU32(background), metrics.controlRadius);

    const ImVec2 iconPosition{minimum.x + metrics.spacingMd, minimum.y + (rowHeight - iconSize) * 0.5F};
    if (step.state == ConnectionStepState::Running) {
        ImGui::SetCursorScreenPos({iconPosition.x, minimum.y + (rowHeight - metrics.controlSm) * 0.5F});
        const std::string spinnerId{"connection-step-spinner-" + std::to_string(index)};
        px::ui::LoadingSpinner({spinnerId}, iconSize * 0.42F);
    } else {
        const px::ui::VectorIcon icon{
            step.state == ConnectionStepState::Succeeded
                ? px::ui::VectorIcon::CircleCheck
                : (step.state == ConnectionStepState::Failed ? px::ui::VectorIcon::TriangleAlert : px::ui::VectorIcon::Info)};
        const ImVec4 color{step.state == ConnectionStepState::Succeeded
                               ? tokens.success
                               : (step.state == ConnectionStepState::Failed ? tokens.destructive : tokens.mutedForeground)};
        px::ui::DrawVectorIcon(icon, iconPosition, iconSize, ImGui::GetColorU32(color));
    }

    ImGui::SetCursorScreenPos({textLeft, minimum.y + metrics.spacingSm});
    px::ui::StrongText(localizer.Text(StepText(step.kind)));
    const float badgeWidth{112.0F * metrics.scale};
    ImGui::SameLine();
    ImGui::SetCursorScreenPos({minimum.x + width - badgeWidth, minimum.y + metrics.spacingSm - metrics.spacingXs});
    px::ui::StatusBadge(StatusText(localizer, step.state), BadgeFor(step.state));
    if (!step.detail.empty()) {
        ImGui::SetCursorScreenPos({textLeft, minimum.y + metrics.spacingSm + ImGui::GetTextLineHeight() + metrics.spacingXs});
        ImGui::PushTextWrapPos(minimum.x + width - metrics.spacingMd);
        px::ui::MutedText(step.detail);
        ImGui::PopTextWrapPos();
    }
    ImGui::SetCursorScreenPos({minimum.x, minimum.y + rowHeight});
    ImGui::Dummy({width, metrics.spacingXs});
}

std::optional<ConnectionProgressStep> VisibleStep(const px::ui::Localizer& localizer, const ConnectionProgress& progress) {
    const auto failed = std::ranges::find(progress.steps, ConnectionStepState::Failed, &ConnectionProgressStep::state);
    if (failed != progress.steps.end()) {
        auto result = *failed;
        result.detail = std::string{localizer.Text(FailureText(progress.failure))};
        return result;
    }
    if (const auto running = std::ranges::find(progress.steps, ConnectionStepState::Running, &ConnectionProgressStep::state);
        running != progress.steps.end()) {
        return *running;
    }
    for (auto completed = progress.steps.rbegin(); completed != progress.steps.rend(); ++completed) {
        if (completed->state == ConnectionStepState::Succeeded)
            return *completed;
    }
    return std::nullopt;
}

} // namespace

ConnectionProgressDialog::ConnectionProgressDialog(std::shared_ptr<RemoteControlPort> port) : port_{std::move(port)} {}

void ConnectionProgressDialog::Draw(const px::ui::Localizer& localizer) {
    const auto progress = port_->ConnectionProgressSnapshot();
    if (!progress)
        return;
    if (progress->generation != observedGeneration_) {
        observedGeneration_ = progress->generation;
        if (progress->status != ConnectionProgressStatus::Succeeded)
            px::ui::OpenModal({"ConnectionProgressDialog"});
    }
    px::ui::ModalScope dialog{{"ConnectionProgressDialog"}, 560.0F};
    if (!dialog.Open())
        return;

    if (progress->status == ConnectionProgressStatus::Succeeded) {
        ImGui::CloseCurrentPopup();
        return;
    }

    const bool running{progress->status == ConnectionProgressStatus::Running};
    const px::ui::BadgeVariant headerTone{progress->status == ConnectionProgressStatus::Failed ? px::ui::BadgeVariant::Destructive
                                                                                               : px::ui::BadgeVariant::Default};
    if (px::ui::DialogHeader(
            {"connection-progress-close"}, localizer.Text(px::ui::TextId::ConnectionProgressTitle), IntentDescription(localizer, *progress),
            {.icon = progress->intent == ConnectionIntent::FileTransfer ? px::ui::VectorIcon::FileTransfer : px::ui::VectorIcon::Connect,
             .tone = headerTone,
             .closeable = !running})) {
        ImGui::CloseCurrentPopup();
        return;
    }

    if (const auto step = VisibleStep(localizer, *progress))
        DrawStep(localizer, *step, 0);

    if (!running) {
        const float buttonWidth{px::ui::Scale(140.0F)};
        px::ui::DialogFooter(buttonWidth);
        if (px::ui::ActionButton({"connection-progress-confirm"}, localizer.Text(px::ui::TextId::Confirm), {.width = buttonWidth}))
            ImGui::CloseCurrentPopup();
    }
}

} // namespace px::panel::ui
