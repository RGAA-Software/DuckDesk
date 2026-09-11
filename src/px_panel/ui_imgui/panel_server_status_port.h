#pragma once

#include "server_status_port.h"

#include <memory>

namespace px {
class PxApplication;
} // namespace px

namespace px::panel::ui {

std::shared_ptr<ServerStatusPort> CreatePanelServerStatusPort(const std::shared_ptr<PxApplication>& application);

} // namespace px::panel::ui
