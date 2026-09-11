#pragma once

#include <cstdint>

namespace px::panel::ui {

enum class PanelPage : std::uint8_t {
    RemoteControl,
    CloudApplications,
    ServerStatus,
    Security,
    Settings,
    Hardware,
};

} // namespace px::panel::ui
