#pragma once

#include "px_common/iroh_connection_description.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "px_common/secret_buffer.h"
#include "px_ui/device_platform.h"

namespace px::panel::product {

enum class ConnectionInputKind : std::uint8_t {
    DeviceCode,
    SharedLink,
    DirectEndpoint,
};

struct ParsedConnectionInput final {
    ConnectionInputKind kind{ConnectionInputKind::DeviceCode};
    std::string deviceId{};
    std::string publicDeviceCode{};
    std::string consoleOrigin{};
    std::string displayName{};
    px::ui::DevicePlatform platform{px::ui::DevicePlatform::Unknown};
    std::vector<std::string> hosts{};
    int port{};
    std::string password{};
    std::string frontendSessionId{};
    std::int64_t frontendSessionRevision{};
    std::shared_ptr<const px::SecretBuffer> frontendToken{};
    std::optional<px::IrohConnectionDescription> iroh{};
    std::string relayHost{};
    int relayPort{};
    std::string relayDeviceId{};
    std::string relayAdmissionTicket{};
};

[[nodiscard]] std::optional<ParsedConnectionInput> ParseConnectionInput(std::string value, int defaultPort);
[[nodiscard]] bool ConnectionInputNeedsPassword(const std::string& value);
[[nodiscard]] bool ConnectionIdentityMatches(const ParsedConnectionInput& target, std::string_view actualDeviceId,
                                             std::string_view actualPublicCode, std::string_view actualConsoleOrigin);

}  // namespace px::panel::product
