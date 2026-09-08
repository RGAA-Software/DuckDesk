#ifndef PX_CLIENT_SDK_NEW_CONNECTION_SDK_WEBSOCKET_RECONNECT_H
#define PX_CLIENT_SDK_NEW_CONNECTION_SDK_WEBSOCKET_RECONNECT_H

#include <string>
#include <cstdint>
#include <memory>
#include <utility>

#include "px_common/websocket_reconnect_adapter.h"
#include "px_common/reconnect_supervisor.h"
#include "px_common/ws_control_signal.h"

namespace px {

// Apply to every ingress message before either business dispatch or a terminal rejection side effect.
inline bool CanDeliverSdkWebSocketMessage(const std::shared_ptr<PxReconnectSupervisor>& supervisor, std::uint64_t generation) {
    return supervisor && generation != 0 && supervisor->Generation() == generation && supervisor->IsReady();
}

inline PxAsyncError MakeSdkWebSocketRejectionError(const WsControlRejection rejection) {
    std::string message;
    switch (rejection) {
        case WsControlRejection::kAuthorization:
            message = "websocket authorization was rejected";
            break;
        case WsControlRejection::kOccupied:
            message = "websocket session is occupied";
            break;
        case WsControlRejection::kSessionPolicy:
            message = "websocket session policy rejected the connection";
            break;
        case WsControlRejection::kNone:
        default:
            message = "websocket session was rejected";
            break;
    }
    return MakePxAsyncError(
        PxAsyncErrorCode::kProtocolError,
        "sdk.websocket.rejection",
        std::move(message),
        false,
        "SDK_WEBSOCKET_SESSION_REJECTED");
}

} // namespace px

#endif // PX_CLIENT_SDK_NEW_CONNECTION_SDK_WEBSOCKET_RECONNECT_H
