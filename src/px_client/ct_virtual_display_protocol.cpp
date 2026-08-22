#include "ct_virtual_display_protocol.h"

#include <atomic>
#include <format>

namespace px {

    std::string NextNativeVirtualDisplayRequestId(uint64_t process_id) {
        static std::atomic_uint64_t sequence = 0;
        return std::format(
            "native-{}-{}", process_id,
            sequence.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    Message MakeVirtualDisplayRequestMessage(
        const std::string& device_id,
        const std::string& stream_id,
        const std::string& request_id,
        RemoteVirtualDisplayOperation operation,
        uint32_t width,
        uint32_t height,
        uint32_t refresh_hz) {
        Message message;
        message.set_type(kVirtualDisplayRequest);
        message.set_device_id(device_id);
        message.set_stream_id(stream_id);
        auto* request = message.mutable_virtual_display_request();
        request->set_request_id(request_id);
        request->set_operation(operation);
        request->set_width(width);
        request->set_height(height);
        request->set_refresh_hz(refresh_hz);
        return message;
    }

}
