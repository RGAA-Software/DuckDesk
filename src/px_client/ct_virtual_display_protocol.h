#ifndef GAMMARAYPC_CT_VIRTUAL_DISPLAY_PROTOCOL_H
#define GAMMARAYPC_CT_VIRTUAL_DISPLAY_PROTOCOL_H

#include <cstdint>
#include <string>

#include "px_message.pb.h"

namespace px {

    std::string NextNativeVirtualDisplayRequestId(uint64_t process_id);

    // Native transports keep carrying configuration and video while the
    // controlled host applies a display-topology change.  The wire-level
    // NEED_RECONNECT state is for clients (notably the browser RTC client)
    // that must rebuild their media session.  Treat it as READY in the native
    // UI so a healthy native session is not covered by a false reconnect page.
    VirtualDisplayResponseState NormalizeNativeVirtualDisplayResponseState(
        bool accepted,
        VirtualDisplayResponseState state);

    Message MakeVirtualDisplayRequestMessage(
        const std::string& device_id,
        const std::string& stream_id,
        const std::string& request_id,
        RemoteVirtualDisplayOperation operation,
        uint32_t width,
        uint32_t height,
        uint32_t refresh_hz);

}

#endif // GAMMARAYPC_CT_VIRTUAL_DISPLAY_PROTOCOL_H
