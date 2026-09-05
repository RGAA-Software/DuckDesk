#ifndef PX_WS_CONTROL_SIGNAL_H
#define PX_WS_CONTROL_SIGNAL_H

#include <string_view>

namespace px {

inline constexpr std::string_view kWsAuthorizationRejectedSignal =
    "px-control:authorization-rejected";
inline constexpr std::string_view kWsSessionOccupiedSignal =
    "px-control:session-occupied";
inline constexpr std::string_view kWsSessionRejectedSignal =
    "px-control:session-rejected";
// Sent by an already authenticated UDP-direct client over its reliable
// WebSocket control channel. Render stops filtering media on that same socket,
// so UDP fallback never has to redeem the one-time connection ticket again.
inline constexpr std::string_view kWsUseWebSocketMediaSignal =
    "px-control:use-websocket-media";

enum class WsControlRejection {
    kNone,
    kAuthorization,
    kOccupied,
    kSessionPolicy,
};

inline WsControlRejection ParseWsControlRejection(const std::string_view value) {
    if (value == kWsAuthorizationRejectedSignal) {
        return WsControlRejection::kAuthorization;
    }
    if (value == kWsSessionOccupiedSignal) {
        return WsControlRejection::kOccupied;
    }
    if (value == kWsSessionRejectedSignal) {
        return WsControlRejection::kSessionPolicy;
    }
    return WsControlRejection::kNone;
}

inline bool IsWsUseWebSocketMediaSignal(const std::string_view value) {
    return value == kWsUseWebSocketMediaSignal;
}

} // namespace px

#endif // PX_WS_CONTROL_SIGNAL_H
