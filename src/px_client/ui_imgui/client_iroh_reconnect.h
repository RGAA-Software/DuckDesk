#pragma once

#include "client_launch_config.h"

namespace px::client::imgui {
void BindClientIrohRefresh(std::optional<px::IrohDialParameters>& parameters, const ClientLaunchConfig& config);
}  // namespace px::client::imgui
