#ifndef GAMMARAYPC_CT_VIRTUAL_DISPLAY_PROTOCOL_H
#define GAMMARAYPC_CT_VIRTUAL_DISPLAY_PROTOCOL_H

#include <cstdint>
#include <string>

#include "px_message.pb.h"

namespace px {

    std::string NextNativeVirtualDisplayRequestId(uint64_t process_id);

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
