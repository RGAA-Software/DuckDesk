#pragma once

#include <optional>
#include <string>

namespace px::panel::ui {

struct PortRange final {
    int first{};
    int last{};
};

struct NetworkSettingsDraft final {
    std::string consoleAddress{};
    bool consoleAddressEditable{true};
    std::optional<int> consolePort{};
    int serviceManagementPort{4603};
    int desktopConnectionPort{4601};
    PortRange applicationPorts{4613, 4998};
    PortRange rtcPorts{5000, 5031};
    int panelListeningPort{4999};
};

}  // namespace px::panel::ui
