#include "panel_device_presence.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "panel_config_store.h"

namespace px::panel::product {
namespace {

constexpr auto kNetworkFailureGrace = std::chrono::seconds{15};
// A 429 must not masquerade as offline during the Console's one-minute rate window.
constexpr auto kRateLimitGrace = std::chrono::seconds{75};

}  // namespace

void PanelDevicePresence::Refresh(std::vector<ui::RemoteDeviceCard>& devices, const std::string& selectedConsoleOrigin, const Resolver& resolveDevice,
                                  const Clock::time_point observedAt) {
    const auto selectedConsole = ParseConsoleHttpsOrigin(selectedConsoleOrigin);
    const std::string currentOrigin{selectedConsole ? selectedConsole->baseUrl : std::string{}};
    if (consoleOrigin_ != currentOrigin) {
        observations_.clear();
        consoleOrigin_ = currentOrigin;
    }
    std::unordered_map<std::string, LookupResult> resolvedDevices{};
    std::unordered_set<std::string> retainedCodes{};
    for (auto& device : devices) {
        device.online = false;
        const auto deviceConsole = ParseConsoleHttpsOrigin(device.consoleOrigin);
        if (!selectedConsole || !deviceConsole || selectedConsole->baseUrl != deviceConsole->baseUrl || device.deviceId.empty() ||
            device.publicDeviceCode.size() != 9 ||
            !std::ranges::all_of(device.publicDeviceCode, [](const char digit) { return digit >= '0' && digit <= '9'; }) || !resolveDevice) {
            continue;
        }
        retainedCodes.insert(device.publicDeviceCode);
        auto resolvedDevice = resolvedDevices.find(device.publicDeviceCode);
        if (resolvedDevice == resolvedDevices.end()) {
            resolvedDevice = resolvedDevices.emplace(device.publicDeviceCode, resolveDevice(device.publicDeviceCode)).first;
        }
        const auto& lookup = resolvedDevice->second;
        if (lookup) {
            if (lookup->device_id.empty() || lookup->public_code != device.publicDeviceCode || lookup->host.empty() || lookup->port <= 0 ||
                lookup->port > 65535) {
                observations_.erase(device.publicDeviceCode);
                continue;
            }
            observations_.insert_or_assign(device.publicDeviceCode, Observation{.endpoint = *lookup, .confirmedAt = observedAt});
        } else {
            const auto failure = lookup.error();
            const bool retryable{
                failure == px_console::ConsoleApiError::kNetworkUnavailable || failure == px_console::ConsoleApiError::kRateLimited ||
                failure == px_console::ConsoleApiError::kServiceUnavailable || failure == px_console::ConsoleApiError::kInternalError};
            if (!retryable) {
                observations_.erase(device.publicDeviceCode);
                continue;
            }
        }
        const auto observation = observations_.find(device.publicDeviceCode);
        if (observation == observations_.end() || observation->second.endpoint.device_id != device.deviceId) {
            continue;
        }
        const auto freshness = observedAt - observation->second.confirmedAt;
        const auto maximumAge = !lookup && lookup.error() == px_console::ConsoleApiError::kRateLimited ? kRateLimitGrace : kNetworkFailureGrace;
        if (freshness < Clock::duration::zero() || freshness >= maximumAge) {
            observations_.erase(observation);
            continue;
        }
        device.online = true;
        device.host = observation->second.endpoint.host;
        device.port = observation->second.endpoint.port;
    }
    std::erase_if(observations_, [&retainedCodes](const auto& observation) { return !retainedCodes.contains(observation.first); });
}

}  // namespace px::panel::product
