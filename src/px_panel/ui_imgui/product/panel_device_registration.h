#pragma once

#include <memory>

namespace px::panel::product {

class PanelProductRuntime;

bool EnsurePanelDeviceRegistration(const std::shared_ptr<PanelProductRuntime>& runtime);

} // namespace px::panel::product
