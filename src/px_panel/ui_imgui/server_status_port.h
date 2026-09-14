#pragma once

#include <cstdint>
#include <memory>
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
};

class ServerStatusPort {
  public:
    virtual ~ServerStatusPort() = default;
    virtual ServerStatusState Snapshot() const = 0;
    virtual void RestartRender() = 0;
    virtual void InstallControllerDriver() = 0;
};

std::shared_ptr<ServerStatusPort> CreatePreviewServerStatusPort();

} // namespace px::panel::ui
