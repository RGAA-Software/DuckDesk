#include "server_status_page.h"

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace px::panel::ui {
namespace {

std::string FormatBytes(const std::uint64_t bytes) {
    constexpr double bytesPerGigabyte{1024.0 * 1024.0 * 1024.0};
    return std::format("{:.1f} GB", static_cast<double>(bytes) / bytesPerGigabyte);
}

float UsagePercent(const std::uint64_t usedBytes, const std::uint64_t totalBytes) noexcept {
    if (totalBytes == 0)
        return 0.0F;
    return std::clamp(static_cast<float>(static_cast<double>(usedBytes) * 100.0 / static_cast<double>(totalBytes)), 0.0F, 100.0F);
}

ImVec4 UsageColor(const float percent) noexcept {
    const auto tokens = px::ui::CurrentThemeTokens();
    if (percent < 50.0F)
        return tokens.success;
    if (percent < 80.0F)
        return tokens.warning;
    return tokens.destructive;
}

void DrawMetricRow(const std::string_view label, const std::string_view value, const std::optional<float> percent = std::nullopt) {
    px::ui::MutedText(label);
    ImGui::SameLine(px::ui::Scale(120.0F));
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
    if (!percent)
        return;
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, UsageColor(*percent));
    ImGui::Text("%.1f%%", *percent);
    ImGui::PopStyleColor();
}

void DrawEnvironmentRow(const px::ui::Localizer& localizer, const px::ui::TextId label, const bool known, const std::string_view detail) {
    ImGui::AlignTextToFramePadding();
    px::ui::MutedText(localizer.Text(label));
    ImGui::SameLine(px::ui::Scale(190.0F));
    if (known) {
        ImGui::SetWindowFontScale(0.875F);
        px::ui::StatusBadge(localizer.Text(px::ui::TextId::Ready), px::ui::BadgeVariant::Success);
        ImGui::SetWindowFontScale(1.0F);
    } else {
        px::ui::MutedText(localizer.Text(px::ui::TextId::Unknown));
    }
    ImGui::SameLine(px::ui::Scale(285.0F));
    ImGui::TextUnformatted(detail.data(), detail.data() + detail.size());
}

} // namespace

ServerStatusPage::ServerStatusPage(std::shared_ptr<ServerStatusPort> port) : port_{std::move(port)} {}

void ServerStatusPage::DrawStatusRow(const px::ui::Localizer& localizer, const float width, const px::ui::TextId label, const bool ready,
                                     const bool canAct, const px::ui::TextId action, const std::function<void()>& onAction) const {
    constexpr ImGuiWindowFlags cardFlags{ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};
    px::ui::CardScope card{{std::string{"status-"} + std::to_string(static_cast<int>(label))}, {width, px::ui::Scale(56.0F)}, cardFlags};
    if (!card.Visible())
        return;

    ImGui::AlignTextToFramePadding();
    px::ui::MutedText(localizer.Text(label));
    ImGui::SameLine();
    ImGui::SetWindowFontScale(0.875F);
    px::ui::StatusBadge(localizer.Text(ready ? px::ui::TextId::Ready : px::ui::TextId::Unavailable),
                        ready ? px::ui::BadgeVariant::Success : px::ui::BadgeVariant::Destructive);
    if (canAct) {
        ImGui::SameLine();
        if (px::ui::ActionButton({"status-action"}, localizer.Text(action),
                                 {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Xs, .circular = true})) {
            onAction();
        }
    }
    ImGui::SetWindowFontScale(1.0F);
}

void ServerStatusPage::Draw(const px::ui::Localizer& localizer) {
    const auto state = port_->Snapshot();
    px::ui::PageTitle(localizer.Text(px::ui::TextId::ServerStatus));
    ImGui::SameLine(0.0F, px::ui::Scale(14.0F));
    static_cast<void>(px::ui::ActionButton(
        {"runtime-environment-refresh"}, localizer.Text(px::ui::TextId::Refresh),
        {.variant = px::ui::ButtonVariant::Outline, .size = px::ui::WidgetSize::Xs, .icon = px::ui::VectorIcon::Refresh, .circular = true}));
    const float statusCardWidth{(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F};
    DrawStatusRow(localizer, statusCardWidth, px::ui::TextId::RenderService, state.renderReady, true, px::ui::TextId::Restart,
                  [port = port_] { port->RestartRender(); });
    ImGui::SameLine();
    DrawStatusRow(localizer, statusCardWidth, px::ui::TextId::NodeService, state.serviceReady, false, px::ui::TextId::Install, [] {});
    ImGui::Spacing();
    {
        constexpr ImGuiWindowFlags cardFlags{ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};
        constexpr ImGuiChildFlags childFlags{ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY};
        const auto metrics = px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi);
        px::ui::CardScope hardware{{"status-hardware"}, {0.0F, 0.0F}, cardFlags, childFlags, ImVec2{metrics.spacingLg, px::ui::Scale(12.0F)}};
        if (hardware.Visible()) {
            px::ui::SectionTitle(localizer.Text(px::ui::TextId::Hardware));
            px::ui::HorizontalSeparator();
            if (!state.machine.available) {
                px::ui::MutedText(localizer.Text(px::ui::TextId::MachineInformationUnavailable));
            } else {
                DrawMetricRow(localizer.Text(px::ui::TextId::Processor), state.machine.cpuName.empty() ? "-" : state.machine.cpuName,
                              state.machine.cpuUsagePercent);
                DrawMetricRow(localizer.Text(px::ui::TextId::Memory),
                              std::format("{} / {}", FormatBytes(state.machine.memoryUsedBytes), FormatBytes(state.machine.memoryTotalBytes)),
                              UsagePercent(state.machine.memoryUsedBytes, state.machine.memoryTotalBytes));
                if (state.machine.disks.empty()) {
                    DrawMetricRow(localizer.Text(px::ui::TextId::Storage), "-");
                } else {
                    for (const auto& disk : state.machine.disks) {
                        const auto usedBytes = disk.totalBytes - std::min(disk.availableBytes, disk.totalBytes);
                        DrawMetricRow(localizer.Text(px::ui::TextId::Storage),
                                      std::format("{}  {} / {}", disk.mountPoint.empty() ? "-" : disk.mountPoint, FormatBytes(usedBytes),
                                                  FormatBytes(disk.totalBytes)),
                                      UsagePercent(usedBytes, disk.totalBytes));
                    }
                }
                if (state.machine.gpus.empty()) {
                    DrawMetricRow(localizer.Text(px::ui::TextId::GraphicsCard), "-");
                } else {
                    for (const auto& gpu : state.machine.gpus) {
                        std::string description{gpu.name.empty() ? "-" : gpu.name};
                        if (gpu.memoryTotalBytes > 0) {
                            description += std::format("  {} / {}", FormatBytes(gpu.memoryUsedBytes), FormatBytes(gpu.memoryTotalBytes));
                        }
                        DrawMetricRow(localizer.Text(px::ui::TextId::GraphicsCard), description, static_cast<float>(gpu.utilizationPercent));
                        if (!gpu.driverVersion.empty()) {
                            DrawMetricRow(localizer.Text(px::ui::TextId::GraphicsDriver),
                                          std::format("{}  {}", gpu.name.empty() ? "-" : gpu.name, gpu.driverVersion));
                        }
                    }
                }
            }
        }
    }
    ImGui::Spacing();
    {
        constexpr ImGuiWindowFlags cardFlags{ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};
        constexpr ImGuiChildFlags childFlags{ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY};
        const auto metrics = px::ui::MetricsFor(ImGui::GetStyle().FontScaleDpi);
        px::ui::CardScope software{{"status-software"}, {0.0F, 0.0F}, cardFlags, childFlags,
                                   ImVec2{metrics.spacingLg, px::ui::Scale(12.0F)}};
        if (software.Visible()) {
            px::ui::SectionTitle(localizer.Text(px::ui::TextId::SoftwareEnvironment));
            px::ui::HorizontalSeparator();
            const bool hasOperatingSystem{!state.machine.operatingSystem.empty()};
            DrawEnvironmentRow(localizer, px::ui::TextId::SystemVersion, hasOperatingSystem,
                               hasOperatingSystem ? state.machine.operatingSystem : localizer.Text(px::ui::TextId::Unknown));
            DrawEnvironmentRow(localizer, px::ui::TextId::AudioService, false, localizer.Text(px::ui::TextId::Unknown));
            DrawEnvironmentRow(localizer, px::ui::TextId::VisualCppRuntime, false, localizer.Text(px::ui::TextId::Unknown));
            DrawEnvironmentRow(localizer, px::ui::TextId::DirectXRuntime, false, localizer.Text(px::ui::TextId::Unknown));
            DrawEnvironmentRow(localizer, px::ui::TextId::WindowsAutoLogin, false, localizer.Text(px::ui::TextId::Unknown));
            DrawEnvironmentRow(localizer, px::ui::TextId::NeverTurnOffDisplay, false, localizer.Text(px::ui::TextId::Unknown));
            DrawEnvironmentRow(localizer, px::ui::TextId::SystemNeverSleeps, false, localizer.Text(px::ui::TextId::Unknown));
            DrawEnvironmentRow(localizer, px::ui::TextId::HighPerformanceMode, false, localizer.Text(px::ui::TextId::Unknown));
        }
    }
    ImGui::Dummy({0.0F, px::ui::Scale(12.0F)});
}

} // namespace px::panel::ui
