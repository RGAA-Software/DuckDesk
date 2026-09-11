#pragma once

#include <string>

namespace px {

enum class SdkSessionMode { kNative, kRdp };
enum class SdkMediaTransport { kUdp, kWebSocket };
enum class SdkConnectionRoute { kDirect, kWebSocketRelay };

// Value configuration for the native transport. No renderer, codec, OS handle or
// UI state belongs here. NetClient owns an immutable snapshot for one session;
// a new authorization attempt creates a new client with its new credentials.
struct SdkConnectionParams final {
    SdkSessionMode session_mode_{SdkSessionMode::kNative};
    SdkMediaTransport media_transport_{SdkMediaTransport::kUdp};
    SdkConnectionRoute route_{SdkConnectionRoute::kDirect};
    bool ssl_{false};
    bool enable_audio_{false};
    bool enable_video_{false};
    bool file_transfer_only_{false};
    std::string ip_{};
    int port_{0};
    int udp_port_{20371};
    std::string media_path_{};
    std::string ft_path_{};
    std::string device_id_{};
    std::string stream_id_{};
    std::string relay_host_{};
    int relay_port_{0};
    std::string relay_device_id_{};
    std::string relay_remote_device_id_{};
    std::string relay_ticket_device_id_{};
    std::string device_name_{};
    std::string appkey_{};
    bool force_gdi_{false};

    // Short-lived authorization material: never persist or log these values.
    std::string connection_ticket_{};
    std::string connection_nonce_{};
    std::string connection_instance_id_{};
    // Associates UDP with the authenticated WS binding; not a session grant.
    // An empty value asks NetClient to generate a session-local association.
    std::string udp_media_association_{};
};

} // namespace px
