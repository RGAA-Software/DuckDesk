#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace px::panel::product {

enum class ConnectionInputKind : std::uint8_t {
    DeviceId,
    SharedLink,
    DirectEndpoint,
};

struct ParsedConnectionInput final {
    ConnectionInputKind kind{ConnectionInputKind::DeviceId};
    std::string deviceId{};
    std::string displayName{};
    std::vector<std::string> hosts{};
    int port{};
    std::string password{};
    std::string relayHost{};
    int relayPort{};
    std::string relayDeviceId{};
};

[[nodiscard]] std::optional<ParsedConnectionInput> ParseConnectionInput(std::string value, int defaultPort);
[[nodiscard]] bool ConnectionInputNeedsPassword(const std::string& value);

} // namespace px::panel::product
