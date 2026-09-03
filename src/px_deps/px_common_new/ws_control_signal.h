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

} // namespace px

#endif // PX_WS_CONTROL_SIGNAL_H
