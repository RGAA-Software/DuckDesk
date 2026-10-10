#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "px_common/iroh_connection_description.h"

namespace px {

enum class SdkSessionMode { kNative, kRdp };
enum class SdkMediaTransport { kUdp, kWebSocket, kIroh };
enum class SdkConnectionRoute { kDirect, kWebSocketRelay };

struct IrohEndpointRefreshResult final {
    std::optional<IrohConnectionDescription> description{};
    std::string terminal_error{};
};

struct IrohDialParameters final {
    std::string endpoint_address{};
    std::string endpoint_configuration{"{}"};
    // Existing resource-session/frontend parameters; this transport creates no additional credentials.
    std::unordered_map<std::string, std::string> frontend{};
    // Composition supplies a bounded lookup for this same resource session.
    std::function<IrohEndpointRefreshResult()> refresh_endpoint{};
    // Local retry ceiling, not an extension of the server's application grace or session lease.
    std::chrono::milliseconds reconnect_timeout{std::chrono::seconds(30)};
};

// Value configuration for the native transport. No renderer, codec, OS handle or
// UI state belongs here. NetClient owns an immutable snapshot for one session;
// a new authorization attempt creates a new client with its new credentials.
struct SdkConnectionParams final {
    std::optional<IrohDialParameters> iroh_{};
    SdkSessionMode session_mode_{SdkSessionMode::kNative};
    SdkMediaTransport media_transport_{SdkMediaTransport::kUdp};
    SdkConnectionRoute route_{SdkConnectionRoute::kDirect};
    bool ssl_{false};
    bool enable_audio_{false};
    bool enable_video_{false};
    bool file_transfer_only_{false};
    std::string ip_{};
    int port_{0};
    std::string media_path_{};
    std::string ft_path_{};
    std::string device_id_{};
    std::string stream_id_{};
    std::string relay_host_{};
    int relay_port_{0};
    std::string relay_device_id_{};
    std::string relay_remote_device_id_{};
    std::string remote_password_hash_{};
    std::string device_name_{};
    std::string appkey_{};
    bool force_gdi_{false};

    std::string connection_nonce_{};
    std::string connection_instance_id_{};
    // Associates UDP with the authenticated WS binding; not a session grant.
    // An empty value asks NetClient to generate a session-local association.
    std::string udp_media_association_{};
};

}  // namespace px
