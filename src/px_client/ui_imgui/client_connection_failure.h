#pragma once

#include <cstdint>
#include <string_view>

#include "client_text.h"

namespace px::client::imgui {

enum class ClientConnectionFailure : std::uint8_t {
    None,
    Authorization,
    RemoteAccessDisabled,
    Occupied,
    SessionPolicy,
    TakenOver,
    Transport,
    ReconnectExpired,
    SessionEnded
};

inline ClientConnectionFailure IrohConnectionFailure(const std::string_view code) noexcept {
    if (code == "IROH_RECONNECT_EXPIRED") return ClientConnectionFailure::ReconnectExpired;
    if (code == "IROH_SESSION_ENDED" || code == "IROH_REMOTE_CLOSED") return ClientConnectionFailure::SessionEnded;
    if (code == "SESSION_OCCUPIED") return ClientConnectionFailure::Occupied;
    if (code == "REMOTE_ACCESS_DISABLED") return ClientConnectionFailure::RemoteAccessDisabled;
    if (code == "SESSION_ADMISSION_DENIED" || code == "SESSION_CAPABILITY_DENIED") return ClientConnectionFailure::SessionPolicy;
    if (code == "AUTHORIZATION_REJECTED" || code == "SESSION_PASSWORD_REJECTED") return ClientConnectionFailure::Authorization;
    return ClientConnectionFailure::Transport;
}

inline ClientText ConnectionFailureText(const ClientConnectionFailure failure) noexcept {
    switch (failure) {
        case ClientConnectionFailure::Authorization:
            return ClientText::AuthorizationRejected;
        case ClientConnectionFailure::RemoteAccessDisabled:
            return ClientText::RemoteAccessDisabled;
        case ClientConnectionFailure::Occupied:
            return ClientText::DeviceOccupied;
        case ClientConnectionFailure::SessionPolicy:
            return ClientText::SessionPolicyRejected;
        case ClientConnectionFailure::TakenOver:
            return ClientText::SessionTakenOver;
        case ClientConnectionFailure::Transport:
            return ClientText::TransportRejected;
        case ClientConnectionFailure::ReconnectExpired:
            return ClientText::ReconnectExpired;
        case ClientConnectionFailure::SessionEnded:
            return ClientText::SessionEnded;
        case ClientConnectionFailure::None:
            return ClientText::ConnectionRejected;
    }
    return ClientText::ConnectionRejected;
}

}  // namespace px::client::imgui
