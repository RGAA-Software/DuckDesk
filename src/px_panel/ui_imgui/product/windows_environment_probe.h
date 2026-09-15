#pragma once

#include "server_status_port.h"

#include <vector>

namespace px::panel::product {

[[nodiscard]] std::vector<ui::EnvironmentCheckStatus> ProbeWindowsEnvironment();

} // namespace px::panel::product
