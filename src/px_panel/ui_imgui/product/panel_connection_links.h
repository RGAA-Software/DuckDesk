#pragma once

#include "panel_config_store.h"

#include <optional>
#include <string>
#include <vector>

namespace px::panel::product {

struct PanelConnectionLinks final {
    std::string desktop{};
    std::string web{};
};

[[nodiscard]] PanelConnectionLinks BuildPanelConnectionLinks(const PanelIdentity& identity, const NodePorts& ports,
                                                             const std::optional<ConsoleEndpoint>& console, const std::string& publicAddress,
                                                             const std::vector<std::string>& localAddresses);

} // namespace px::panel::product
