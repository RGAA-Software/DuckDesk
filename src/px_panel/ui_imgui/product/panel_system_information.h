#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace px::panel::product {

struct PanelDiskInformation final {
    std::string mountPoint{};
    std::uint64_t availableBytes{};
    std::uint64_t totalBytes{};
};

struct PanelGpuInformation final {
    std::string name{};
    std::string driverVersion{};
    std::uint64_t memoryUsedBytes{};
    std::uint64_t memoryTotalBytes{};
    unsigned int utilizationPercent{};
};

struct PanelSystemInformation final {
    std::string operatingSystem{};
    std::string cpuName{};
    float cpuUsagePercent{};
    std::uint64_t memoryUsedBytes{};
    std::uint64_t memoryTotalBytes{};
    std::vector<PanelDiskInformation> disks{};
    std::vector<PanelGpuInformation> gpus{};
};

[[nodiscard]] std::optional<PanelSystemInformation> ParsePanelSystemInformation(std::string_view payload);

} // namespace px::panel::product
