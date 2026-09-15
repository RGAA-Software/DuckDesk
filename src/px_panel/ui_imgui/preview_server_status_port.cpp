#include "server_status_port.h"

namespace px::panel::ui {
namespace {

class PreviewServerStatusPort final : public ServerStatusPort {
  public:
    ServerStatusState Snapshot() const override {
        return {
            .controllerDriverReady = true,
            .renderReady = true,
            .serviceReady = true,
            .machine = {.available = true,
                        .operatingSystem = "Microsoft Windows 11 Pro 10.0.26100",
                        .cpuName = "AMD Ryzen 9 7950X",
                        .cpuUsagePercent = 18.5F,
                        .memoryUsedBytes = 18'253'611'008ULL,
                        .memoryTotalBytes = 34'359'738'368ULL,
                        .disks = {{"C:\\", 511'000'000'000ULL, 1'000'000'000'000ULL}},
                        .gpus = {{.name = "NVIDIA GeForce RTX 4090",
                                  .driverVersion = "581.15",
                                  .memoryUsedBytes = 4'000'000'000ULL,
                                  .memoryTotalBytes = 24'000'000'000ULL,
                                  .utilizationPercent = 32U}}},
            .addresses = {{"192.168.1.10", true}},
            .panelPort = 4999,
            .renderPort = 4601,
            .audioSamples = 48000,
            .audioChannels = 2,
            .audioBits = 16,
            .environment =
                {.checks = {{.id = EnvironmentCheckId::Audio, .state = EnvironmentCheckState::Ready, .technicalDetail = "WASAPI"},
                            {.id = EnvironmentCheckId::VisualCppRuntime, .state = EnvironmentCheckState::Ready, .technicalDetail = "MSVC v14 x64"},
                            {.id = EnvironmentCheckId::LegacyDirectXRuntime,
                             .state = EnvironmentCheckState::Ready,
                             .technicalDetail = "XInput 1.3 (xinput1_3.dll)"},
                            {.id = EnvironmentCheckId::WindowsAutoLogin, .state = EnvironmentCheckState::NotConfigured},
                            {.id = EnvironmentCheckId::DisplayTimeout, .state = EnvironmentCheckState::Recommendation, .acTimeoutSeconds = 900U},
                            {.id = EnvironmentCheckId::SleepTimeout, .state = EnvironmentCheckState::Ready, .acTimeoutSeconds = 0U},
                            {.id = EnvironmentCheckId::HighPerformanceMode, .state = EnvironmentCheckState::Recommendation},
                            {.id = EnvironmentCheckId::PendingRestart, .state = EnvironmentCheckState::Ready}}},
        };
    }
    void RestartRender() override {}
    void InstallControllerDriver() override {}
    void RefreshEnvironment() override {}
    void PerformEnvironmentAction(EnvironmentAction) override {}
};

} // namespace

std::shared_ptr<ServerStatusPort> CreatePreviewServerStatusPort() {
    return std::make_shared<PreviewServerStatusPort>();
}

} // namespace px::panel::ui
