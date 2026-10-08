#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "px_console_client/console_user_device_api.h"
#include "remote_control_port.h"

namespace px::panel::product {

class PanelDevicePresence final {
public:
    using Clock = std::chrono::steady_clock;
    using LookupResult = px::Result<px_console::ConsolePublicDeviceEndpoint, px_console::ConsoleApiError>;
    using Resolver = std::function<LookupResult(const std::string&)>;

    // Read-only lookup; cached observations are bounded and never authorize a connection.
    void Refresh(std::vector<ui::RemoteDeviceCard>& devices, const std::string& selectedConsoleOrigin, const Resolver& resolveDevice,
                 Clock::time_point observedAt = Clock::now());

private:
    struct Observation final {
        px_console::ConsolePublicDeviceEndpoint endpoint{};
        Clock::time_point confirmedAt{};
    };

    std::string consoleOrigin_{};
    std::unordered_map<std::string, Observation> observations_{};
};

}  // namespace px::panel::product
