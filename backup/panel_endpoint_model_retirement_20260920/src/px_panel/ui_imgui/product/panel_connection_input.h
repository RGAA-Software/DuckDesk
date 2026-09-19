#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "px_common/secret_buffer.h"
#include "px_ui/device_platform.h"

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
    px::ui::DevicePlatform platform{px::ui::DevicePlatform::Unknown};
    std::vector<std::string> hosts{};
    int port{};
    std::string password{};
    std::string frontendSessionId{};
    std::int64_t frontendSessionRevision{};
    std::shared_ptr<const px::SecretBuffer> frontendToken{};
    std::string relayHost{};
    int relayPort{};
    std::string relayDeviceId{};
    std::string relayAdmissionTicket{};
};

[[nodiscard]] std::optional<ParsedConnectionInput> ParseConnectionInput(std::string value, int defaultPort);
[[nodiscard]] bool ConnectionInputNeedsPassword(const std::string& value);

}  // namespace px::panel::product
