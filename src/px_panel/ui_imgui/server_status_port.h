#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace px::panel::ui {

struct NetworkAddress final {
    std::string address{};
    bool wired{false};
};

struct StorageStatus final {
    std::string mountPoint{};
    std::uint64_t availableBytes{};
    std::uint64_t totalBytes{};
};

struct GraphicsStatus final {
    std::string name{};
    std::string driverVersion{};
    std::uint64_t memoryUsedBytes{};
    std::uint64_t memoryTotalBytes{};
    unsigned int utilizationPercent{};
};

struct MachineStatus final {
    bool available{};
    std::string operatingSystem{};
    std::string cpuName{};
    float cpuUsagePercent{};
    std::uint64_t memoryUsedBytes{};
    std::uint64_t memoryTotalBytes{};
    std::vector<StorageStatus> disks{};
    std::vector<GraphicsStatus> gpus{};
};

enum class EnvironmentCheckId : std::uint8_t {
    Audio,
    VisualCppRuntime,
    LegacyDirectXRuntime,
    WindowsAutoLogin,
    DisplayTimeout,
    SleepTimeout,
    HighPerformanceMode,
    PendingRestart,
};

enum class EnvironmentCheckState : std::uint8_t {
    Checking,
    Ready,
    Recommendation,
    Warning,
    NotConfigured,
    Unavailable,
};

enum class EnvironmentAction : std::uint8_t {
    None,
    OpenSoundSettings,
    DownloadVisualCppRuntime,
    DownloadLegacyDirectXRuntime,
    OpenAutoLoginHelp,
    OpenPowerSettings,
    OpenWindowsUpdateSettings,
};

struct EnvironmentCheckStatus final {
    EnvironmentCheckId id{EnvironmentCheckId::Audio};
    EnvironmentCheckState state{EnvironmentCheckState::Checking};
    EnvironmentAction action{EnvironmentAction::None};
    std::string technicalDetail{};
    std::optional<std::uint32_t> acTimeoutSeconds{};
    std::optional<std::uint32_t> dcTimeoutSeconds{};
};

struct EnvironmentDiagnosticsState final {
    bool refreshing{};
    std::uint64_t generation{};
    std::vector<EnvironmentCheckStatus> checks{};
};

struct ServerStatusState final {
    bool controllerDriverReady{false};
    bool renderReady{false};
    bool serviceReady{false};
    MachineStatus machine{};
    std::vector<NetworkAddress> addresses{};
    int panelPort{};
    int renderPort{};
    int audioSamples{};
    int audioChannels{};
    int audioBits{};
    EnvironmentDiagnosticsState environment{};
};

class ServerStatusPort {
  public:
    virtual ~ServerStatusPort() = default;
    virtual ServerStatusState Snapshot() const = 0;
    virtual void RestartRender() = 0;
    virtual void InstallControllerDriver() = 0;
    virtual void RefreshEnvironment() = 0;
    virtual void PerformEnvironmentAction(EnvironmentAction action) = 0;
};

std::shared_ptr<ServerStatusPort> CreatePreviewServerStatusPort();

} // namespace px::panel::ui
