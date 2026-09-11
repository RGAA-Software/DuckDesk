#pragma once

#include "network_settings_port.h"

#include <memory>

namespace px {
class PanelNetworkSettingsController;
class PxApplication;
} // namespace px

namespace px::panel::ui {

std::shared_ptr<NetworkSettingsPort> CreatePanelNetworkSettingsPort(const std::shared_ptr<PxApplication>& application);

} // namespace px::panel::ui
