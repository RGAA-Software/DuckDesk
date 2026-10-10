#pragma once

#include "px_common/iroh_connection_description.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "px_common/secret_buffer.h"
#include "px_client_sdk/sdk_connection_params.h"

namespace px::client::imgui {

struct ClientFileTransferAcceptanceConfig final {
    std::string localSourcePath{};
    std::string remoteDirectory{};
    std::string localDownloadDirectory{};
    bool exerciseCancelRetry{};
    bool exerciseHostRestart{};
};

struct ClientLaunchConfig final {
    std::optional<px::IrohConnectionDescription> iroh{};
    std::string host{};
    std::string localHost{};
    int port{};
    std::string streamId{};
    std::string streamName{};
    std::string localDeviceId{};
    std::string remoteDeviceId{};
    std::string consoleOrigin{};
    std::string remotePlatform{};
    std::string remotePasswordHash{};
    std::string frontendSessionId{};
    std::int64_t frontendSessionRevision{};
    std::shared_ptr<const px::SecretBuffer> frontendToken{};
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
    std::string decoder{"Auto"};
    std::string recordingPath{};
    bool rdp{};
    std::string rdpAccount{};
    std::string rdpDomain{};
    std::string rdpProxyCertificateSha256{};
    std::shared_ptr<const px::SecretBuffer> rdpPassword{};
    std::optional<ClientFileTransferAcceptanceConfig> fileTransferAcceptance{};
    bool audioAcceptance{};
    bool rdpIoErrorAcceptance{};
    bool rdpPeerCloseAcceptance{};
    int panelPort{};
    std::string panelLaunchId{};
};

[[nodiscard]] std::optional<ClientLaunchConfig> ParseClientLaunchEnvelope(std::string_view envelope, bool allowAcceptance = false);
[[nodiscard]] std::optional<px::IrohDialParameters> BuildClientIrohParameters(const ClientLaunchConfig& config);
[[nodiscard]] std::string BuildClientMediaPath(const ClientLaunchConfig& config);
[[nodiscard]] std::string BuildClientFileTransferPath(const ClientLaunchConfig& config);

}  // namespace px::client::imgui
