#pragma once

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace px {

// Public reachability metadata. Business authorization remains in the existing frontend parameters.
struct IrohConnectionDescription final {
    std::string endpoint_address{};
    std::string endpoint_configuration{"{}"};
    std::map<std::string, std::string> relay_names{};
};

inline std::map<std::string, std::string> ParseIrohRelayNames(const nlohmann::json& names) {
    std::map<std::string, std::string> result{};
    if (!names.is_object() || names.size() > 32U) return result;
    const auto containsControl = [](const std::string& text) {
        return std::any_of(text.begin(), text.end(), [](unsigned char character) { return character < 32U || character == 127U; });
    };
    for (const auto& [address, name] : names.items()) {
        if (!name.is_string() || address.size() > 2'048U || !address.starts_with("https://") || containsControl(address)) continue;
        const auto& label = name.get_ref<const std::string&>();
        if (label.empty() || label.size() > 320U || containsControl(label)) continue;
        result.emplace(address, label);
    }
    return result;
}

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
    auto result = IrohConnectionDescription{address->dump(), configuration->dump()};
    if (const auto names = description.find("relay_names"); names != description.end()) result.relay_names = ParseIrohRelayNames(*names);
    return result;
}

inline nlohmann::json IrohConnectionDescriptionJson(const std::optional<IrohConnectionDescription>& description) {
    if (!description) return nullptr;
    nlohmann::json result{{"endpoint_address", nlohmann::json::parse(description->endpoint_address)},
                          {"endpoint_configuration", nlohmann::json::parse(description->endpoint_configuration)}};
    if (!description->relay_names.empty()) result["relay_names"] = description->relay_names;
    return result;
}

}  // namespace px
