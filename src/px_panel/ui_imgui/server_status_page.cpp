#include "server_status_page.h"

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <utility>

namespace px::panel::ui {

ServerStatusPage::ServerStatusPage(std::shared_ptr<ServerStatusPort> port) : port_{std::move(port)} {}

void ServerStatusPage::DrawStatusRow(const px::ui::Localizer& localizer, const px::ui::TextId label, const bool ready, const bool canAct,
                                     const px::ui::TextId action, const std::function<void()>& onAction) const {
    px::ui::CardScope card{{std::string{"status-"} + std::to_string(static_cast<int>(label))}, {px::ui::Scale(224.0F), px::ui::Scale(112.0F)}};
    if (!card.Visible())
        return;
    px::ui::MutedText(localizer.Text(label));
    px::ui::StatusBadge(localizer.Text(ready ? px::ui::TextId::Ready : px::ui::TextId::Unavailable),
                        ready ? px::ui::BadgeVariant::Success : px::ui::BadgeVariant::Destructive);
    if (canAct) {
        ImGui::SameLine();
        if (px::ui::ActionButton({"status-action"}, localizer.Text(action),
                                 {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Sm})) {
            onAction();
        }
    }
}

void ServerStatusPage::Draw(const px::ui::Localizer& localizer) {
    const auto state = port_->Snapshot();
    px::ui::PageTitle(localizer.Text(px::ui::TextId::ServerStatus));
    DrawStatusRow(localizer, px::ui::TextId::ControllerDriver, state.controllerDriverReady, !state.controllerDriverReady, px::ui::TextId::Install,
                  [port = port_] { port->InstallControllerDriver(); });
    ImGui::SameLine();
    DrawStatusRow(localizer, px::ui::TextId::RenderService, state.renderReady, true, px::ui::TextId::Restart,
                  [port = port_] { port->RestartRender(); });
    ImGui::SameLine();
    DrawStatusRow(localizer, px::ui::TextId::NodeService, state.serviceReady, false, px::ui::TextId::Install, [] {});
    ImGui::Spacing();
    {
        px::ui::CardScope clients{{"status-clients"}, {0.0F, px::ui::Scale(54.0F)}};
        if (clients.Visible()) {
            px::ui::MutedText(localizer.Text(px::ui::TextId::ConnectedClients));
            const std::string value{std::to_string(state.connectedClients)};
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(value.c_str()).x);
            px::ui::SectionTitle(value);
        }
    }
    ImGui::Spacing();
    px::ui::CardScope network{{"status-network"}, {0.0F, 0.0F}};
    if (!network.Visible())
        return;
    px::ui::SectionTitle(localizer.Text(px::ui::TextId::NetworkAddresses));
    px::ui::HorizontalSeparator();
    for (const auto& address : state.addresses) {
        ImGui::Text("%s  (%s)", address.address.c_str(), localizer.Text(address.wired ? px::ui::TextId::Wired : px::ui::TextId::Wireless).data());
    }
    px::ui::KeyValueRow(localizer.Text(px::ui::TextId::PanelListeningPort), std::to_string(state.panelPort), px::ui::Scale(190.0F));
    px::ui::KeyValueRow(localizer.Text(px::ui::TextId::DesktopConnectionPort), std::to_string(state.renderPort), px::ui::Scale(190.0F));
}

} // namespace px::panel::ui
