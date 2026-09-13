#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "px_common/secret_buffer.h"

namespace px::client::imgui {

struct ClientLaunchConfig final {
    std::string host{};
    int port{};
    std::string streamId{};
    std::string streamName{};
    std::string localDeviceId{};
    std::string remoteDeviceId{};
    std::string remotePlatform{};
    std::string remotePasswordHash{};
    std::string nonce{};
    std::string instanceId{};
    std::string appKey{};
    std::string relayHost{};
    int relayPort{};
    std::string relayRemoteDeviceId{};
    bool audio{true};
    bool clipboard{true};
    bool viewOnly{};
    bool forceTcp{};
    bool forceRelay{};
    bool fileTransferOnly{};
    bool splitWindows{};
    bool forceGdiCapture{};
    bool disableVulkan{};
    bool waitForDebugger{};
    std::string language{"zh-CN"};
    bool lightTheme{};
    bool enhancedVisualEffects{true};
    std::string decoder{"Auto"};
    std::string recordingPath{};
    bool rdp{};
    std::string rdpAccount{};
    std::string rdpDomain{};
    std::string rdpProxyCertificateSha256{};
    std::shared_ptr<const px::SecretBuffer> rdpPassword{};
};

[[nodiscard]] std::optional<ClientLaunchConfig> ParseClientLaunchEnvelope(std::string_view envelope);

} // namespace px::client::imgui
