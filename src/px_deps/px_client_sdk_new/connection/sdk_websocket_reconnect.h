#ifndef PX_CLIENT_SDK_NEW_CONNECTION_SDK_WEBSOCKET_RECONNECT_H
#define PX_CLIENT_SDK_NEW_CONNECTION_SDK_WEBSOCKET_RECONNECT_H

#include <string>
#include <utility>

#include "px_common_new/websocket_reconnect_adapter.h"
#include "px_common_new/ws_control_signal.h"

namespace px {

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
