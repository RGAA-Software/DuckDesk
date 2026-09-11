#include "server_status_page.h"

#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <format>
#include <utility>

namespace px::panel::ui {

ServerStatusPage::ServerStatusPage(std::shared_ptr<ServerStatusPort> port) : port_{std::move(port)} {}

void ServerStatusPage::DrawStatusRow(const px::ui::Localizer& localizer, const px::ui::TextId label, const bool ready, const bool canAct,
                                     const px::ui::TextId action, const std::function<void()>& onAction) const {
    ImGui::TextUnformatted(localizer.Text(label).data());
    ImGui::SameLine(px::ui::Scale(260.0F));
    ImGui::TextColored(ready ? ImVec4{0.18F, 0.78F, 0.36F, 1.0F} : ImVec4{0.90F, 0.22F, 0.28F, 1.0F}, "%s",
                       localizer.Text(ready ? px::ui::TextId::Ready : px::ui::TextId::Unavailable).data());
    if (canAct) {
        ImGui::SameLine(px::ui::Scale(390.0F));
        if (ImGui::SmallButton(localizer.Text(action).data())) {
            onAction();
        }
    }
}

void ServerStatusPage::Draw(const px::ui::Localizer& localizer) {
    const auto state = port_->Snapshot();
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::ServerStatus).data());
    ImGui::Separator();
    ImGui::Spacing();
    DrawStatusRow(localizer, px::ui::TextId::ControllerDriver, state.controllerDriverReady, !state.controllerDriverReady,
                  px::ui::TextId::Install, [port = port_] { port->InstallControllerDriver(); });
    DrawStatusRow(localizer, px::ui::TextId::RenderService, state.renderReady, true, px::ui::TextId::Restart,
                  [port = port_] { port->RestartRender(); });
    DrawStatusRow(localizer, px::ui::TextId::NodeService, state.serviceReady, false, px::ui::TextId::Install, [] {});
    ImGui::Spacing();
    ImGui::SeparatorText(localizer.Text(px::ui::TextId::NetworkAddresses).data());
    for (const auto& address : state.addresses) {
        ImGui::Text("%s  (%s)", address.address.c_str(),
                    localizer.Text(address.wired ? px::ui::TextId::Wired : px::ui::TextId::Wireless).data());
    }
    ImGui::Text("%s: %d", localizer.Text(px::ui::TextId::PanelListeningPort).data(), state.panelPort);
    ImGui::Text("%s: %d", localizer.Text(px::ui::TextId::DesktopConnectionPort).data(), state.renderPort);
    ImGui::Spacing();
    ImGui::SeparatorText(localizer.Text(px::ui::TextId::AudioFormat).data());
    ImGui::Text("%s", std::format("{}/{}/{}", state.audioSamples, state.audioChannels, state.audioBits).c_str());
}

} // namespace px::panel::ui
