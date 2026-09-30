#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "px_console_client/console_user_device_api.h"
#include "remote_control_port.h"

namespace px::panel::product {

// A public lookup is read-only: it neither requires login nor opens a frontend session.
void RefreshConsoleDevicePresence(std::vector<ui::RemoteDeviceCard>& devices, const std::string& selectedConsoleOrigin,
                                  const std::function<std::optional<px_console::ConsolePublicDeviceEndpoint>(const std::string&)>& resolveDevice);

}  // namespace px::panel::product
