#include "panel_system_information.h"

#include "px_common/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace px::panel::product {
namespace {

std::uint64_t UnsignedValue(const nlohmann::json& object, const std::string_view key) {
    const auto found = object.find(std::string{key});
    if (found == object.end() || !found->is_number_unsigned())
        return {};
    return found->get<std::uint64_t>();
}

float FloatValue(const nlohmann::json& object, const std::string_view key) {
    const auto found = object.find(std::string{key});
    if (found == object.end() || !found->is_number())
        return {};
    return found->get<float>();
}

std::string StringValue(const nlohmann::json& object, const std::string_view key) {
    const auto found = object.find(std::string{key});
    if (found == object.end() || !found->is_string())
        return {};
    return found->get<std::string>();
}

std::string TrimWhitespace(std::string value) {
    const auto isWhitespace = [](const unsigned char character) { return std::isspace(character) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isWhitespace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isWhitespace).base();
    if (first >= last)
        return {};
    return {first, last};
}

} // namespace

std::optional<PanelSystemInformation> ParsePanelSystemInformation(const std::string_view payload) {
    if (payload == "Hello, WebSocket!")
        return std::nullopt;
    try {
        const auto root = nlohmann::json::parse(payload);
        if (!root.is_object())
            return std::nullopt;

        PanelSystemInformation result{};
        if (const auto operatingSystem = root.find("os"); operatingSystem != root.end() && operatingSystem->is_object()) {
            result.operatingSystem = StringValue(*operatingSystem, "sys_os_long_version");
            if (result.operatingSystem.empty())
                result.operatingSystem = StringValue(*operatingSystem, "sys_os_version");
        }
        if (const auto cpu = root.find("cpu"); cpu != root.end() && cpu->is_object()) {
            result.cpuName = TrimWhitespace(StringValue(*cpu, "brand"));
            result.cpuUsagePercent = std::clamp(FloatValue(*cpu, "usage"), 0.0F, 100.0F);
        }
        if (const auto memory = root.find("mem"); memory != root.end() && memory->is_object()) {
            result.memoryUsedBytes = UnsignedValue(*memory, "used");
            result.memoryTotalBytes = UnsignedValue(*memory, "total");
        }
        if (const auto disks = root.find("disks"); disks != root.end() && disks->is_array()) {
            for (const auto& disk : *disks) {
                if (!disk.is_object())
                    continue;
                result.disks.push_back({.mountPoint = StringValue(disk, "mount_on"),
                                        .availableBytes = UnsignedValue(disk, "available"),
                                        .totalBytes = UnsignedValue(disk, "total")});
            }
        }
        if (const auto gpus = root.find("gpus"); gpus != root.end() && gpus->is_array()) {
            for (const auto& gpu : *gpus) {
                if (!gpu.is_object())
                    continue;
                const auto utilization = std::min<std::uint64_t>(UnsignedValue(gpu, "gpu_utilization"), 100U);
                result.gpus.push_back({.name = StringValue(gpu, "brand"),
                                       .driverVersion = StringValue(gpu, "driver_version"),
                                       .memoryUsedBytes = UnsignedValue(gpu, "mem_used"),
                                       .memoryTotalBytes = UnsignedValue(gpu, "mem_total"),
                                       .utilizationPercent = static_cast<unsigned int>(utilization)});
            }
        }
        return result;
    } catch (const nlohmann::json::exception& error) {
        LOGW("Ignoring invalid px_osinfo payload: {}", error.what());
        return std::nullopt;
    }
}

} // namespace px::panel::product
