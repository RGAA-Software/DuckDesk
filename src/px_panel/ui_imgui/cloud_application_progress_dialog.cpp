#include "cloud_application_progress_dialog.h"

#include <imgui.h>

#include <array>
#include <format>
#include <utility>

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

namespace px::panel::ui {

CloudApplicationProgressDialog::CloudApplicationProgressDialog(std::shared_ptr<CloudApplicationsPort> port) : port_{std::move(port)} {}

void CloudApplicationProgressDialog::Draw(const px::ui::Localizer& localizer) {
    const auto progress = port_->LaunchProgress();
    if (!progress) return;
    const bool newRequest{observedGeneration_ != progress->generation};
    const bool failed{progress->status == ApplicationLaunchStatus::Failed};
    const bool ready{progress->status == ApplicationLaunchStatus::Ready};
    const bool succeeded{progress->status == ApplicationLaunchStatus::Succeeded};
    const bool launchReady{ready && dispatchedGeneration_ != progress->generation};
    if (newRequest || (failed && displayedStatus_ != ApplicationLaunchStatus::Failed)) {
        observedGeneration_ = progress->generation;
        px::ui::OpenModal({"CloudApplicationProgress"});
    }
    displayedStatus_ = progress->status;
    {
        px::ui::ModalScope dialog{{"CloudApplicationProgress"}, 560.0F};
        if (!dialog.Open()) return;
        if (succeeded) {
            ImGui::CloseCurrentPopup();
        } else {
            static_cast<void>(px::ui::DialogHeader({"application-progress-header"},
                                                   localizer.Text(failed ? px::ui::TextId::OperationFailed : px::ui::TextId::ApplicationLaunchTitle),
                                                   progress->applicationName,
                                                   {.icon = px::ui::VectorIcon::Cloud,
                                                    .tone = failed ? px::ui::BadgeVariant::Destructive : px::ui::BadgeVariant::Default,
                                                    .closeable = false}));
            constexpr std::array labels{px::ui::TextId::ApplicationLaunchStart, px::ui::TextId::ApplicationLaunchWait,
                                        px::ui::TextId::ApplicationLaunchAuthorize, px::ui::TextId::ApplicationLaunchPrepareClient,
                                        px::ui::TextId::ConnectionWaitRemote};
            const auto activeStage{static_cast<std::size_t>(progress->stage)};
            for (std::size_t stageIndex{}; stageIndex < labels.size(); ++stageIndex) {
                const bool complete{stageIndex < activeStage || (ready && stageIndex <= activeStage)};
                const bool current{stageIndex == activeStage};
                px::ui::StrongText(localizer.Text(labels[stageIndex]));
                ImGui::SameLine(px::ui::Scale(330.0F));
                if (current && !ready && !failed) {
                    px::ui::LoadingSpinner({"application-stage-spinner"}, px::ui::Scale(7.0F));
                } else {
                    px::ui::StatusBadge(localizer.Text(complete            ? px::ui::TextId::Succeeded
                                                       : current && failed ? px::ui::TextId::OperationFailed
                                                                           : px::ui::TextId::ConnectionWaiting),
                                        complete            ? px::ui::BadgeVariant::Success
                                        : current && failed ? px::ui::BadgeVariant::Destructive
                                                            : px::ui::BadgeVariant::Secondary);
                }
                ImGui::Dummy({0.0F, px::ui::Scale(10.0F)});
            }
            if (progress->awaitingConnectionRetirement && !failed) {
                ImGui::PushTextWrapPos(0.0F);
                px::ui::MutedText(std::vformat(localizer.Text(px::ui::TextId::ApplicationConnectionRetiring),
                                               std::make_format_args(progress->retirementWaitSeconds)));
                ImGui::PopTextWrapPos();
            }
            if (failed) {
                ImGui::PushTextWrapPos(0.0F);
                px::ui::StrongText(localizer.Text(progress->error));
                if (!progress->diagnostic.empty()) px::ui::MutedText(progress->diagnostic);
                ImGui::PopTextWrapPos();
                const float buttonWidth{px::ui::Scale(112.0F)};
                px::ui::DialogFooter(buttonWidth);
                if (px::ui::ActionButton({"application-progress-dismiss"}, localizer.Text(px::ui::TextId::Confirm), {.width = buttonWidth}))
                    ImGui::CloseCurrentPopup();
            }
        }
    }
    // Keep the modal open while the worker starts Client and awaits its remote acknowledgement.
    if (launchReady) {
        dispatchedGeneration_ = progress->generation;
        port_->LaunchPrepared(progress->generation);
    }
}

}  // namespace px::panel::ui
