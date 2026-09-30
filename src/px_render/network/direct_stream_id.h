#ifndef PX_RENDER_NETWORK_DIRECT_STREAM_ID_H_
#define PX_RENDER_NETWORK_DIRECT_STREAM_ID_H_

#include <string>

#include "px_common/md5.h"

namespace px {

// Deterministic per-node logical-stream UUID lets physical media/file bindings
// renew one Console quota entry without trusting a client-supplied UUID.
inline std::string DirectStreamQuotaId(const std::string& device_id, const std::string& logical_session_id) {
    if (device_id.empty() || logical_session_id.empty()) {
        return {};
    }
    const auto digest = MD5::Hex(device_id + "|" + logical_session_id);
    if (digest.size() != 32) {
        return {};
    }
    return digest.substr(0, 8) + "-" + digest.substr(8, 4) + "-" + digest.substr(12, 4) + "-" + digest.substr(16, 4) + "-" + digest.substr(20, 12);
}

}  // namespace px

#endif  // PX_RENDER_NETWORK_DIRECT_STREAM_ID_H_
