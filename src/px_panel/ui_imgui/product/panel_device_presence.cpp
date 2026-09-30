#include "panel_device_presence.h"

#include <algorithm>
#include <unordered_map>

#include "panel_config_store.h"

namespace px::panel::product {

void RefreshConsoleDevicePresence(std::vector<ui::RemoteDeviceCard>& devices, const std::string& selectedConsoleOrigin,
                                  const std::function<std::optional<px_console::ConsolePublicDeviceEndpoint>(const std::string&)>& resolveDevice) {
    const auto selectedConsole = ParseConsoleHttpsOrigin(selectedConsoleOrigin);
    std::unordered_map<std::string, std::optional<px_console::ConsolePublicDeviceEndpoint>> resolvedDevices{};
    for (auto& device : devices) {
        device.online = false;
        const auto deviceConsole = ParseConsoleHttpsOrigin(device.consoleOrigin);
        if (!selectedConsole || !deviceConsole || selectedConsole->baseUrl != deviceConsole->baseUrl || device.deviceId.empty() ||
            device.publicDeviceCode.size() != 9 ||
            !std::ranges::all_of(device.publicDeviceCode, [](const char digit) { return digit >= '0' && digit <= '9'; }) || !resolveDevice) {
            continue;
        }
        auto resolvedDevice = resolvedDevices.find(device.publicDeviceCode);
        if (resolvedDevice == resolvedDevices.end()) {
            resolvedDevice = resolvedDevices.emplace(device.publicDeviceCode, resolveDevice(device.publicDeviceCode)).first;
        }
        const auto& endpoint = resolvedDevice->second;
        if (!endpoint || endpoint->device_id != device.deviceId || endpoint->public_code != device.publicDeviceCode || endpoint->host.empty() ||
            endpoint->port <= 0 || endpoint->port > 65535) {
            continue;
        }
        device.online = true;
        device.host = endpoint->host;
        device.port = endpoint->port;
    }
}

}  // namespace px::panel::product
