#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace px {

// Public reachability metadata. Business authorization remains in the existing frontend parameters.
struct IrohConnectionDescription final {
    std::string endpoint_address{};
    std::string endpoint_configuration{"{}"};
};

inline std::optional<IrohConnectionDescription> ParseIrohConnectionDescription(const nlohmann::json& description) {
    if (!description.is_object()) return std::nullopt;
    const auto address = description.find("endpoint_address");
    const auto configuration = description.find("endpoint_configuration");
    if (address == description.end() || !address->is_object() || configuration == description.end() || !configuration->is_object())
        return std::nullopt;
    const auto identity = address->find("id");
    const auto routes = address->find("addrs");
    if (identity == address->end() || !identity->is_string() || identity->get_ref<const std::string&>().empty() || routes == address->end() ||
        !routes->is_array() || routes->empty())
        return std::nullopt;
    return IrohConnectionDescription{address->dump(), configuration->dump()};
}

inline nlohmann::json IrohConnectionDescriptionJson(const std::optional<IrohConnectionDescription>& description) {
    if (!description) return nullptr;
    return {{"endpoint_address", nlohmann::json::parse(description->endpoint_address)},
            {"endpoint_configuration", nlohmann::json::parse(description->endpoint_configuration)}};
}

}  // namespace px
