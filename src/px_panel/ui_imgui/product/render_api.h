//
// Created by RGAA on 11/06/2025.
//

#ifndef PX_RENDER_API_H
#define PX_RENDER_API_H

#include "px_common/expected.h"

#include <cstdint>
#include <string>

namespace px {

// Render configuration
class RenderConfiguration {
  public:
    std::string device_id_{};
    std::string relay_host_{};
    int relay_port_{0};
    bool access_policy_known_{};
    bool incoming_remote_access_enabled_{};
    bool file_transfer_enabled_{};
    bool controller_availability_known_{};
    bool controller_available_{};
    bool controller_reconnect_grace_{};
    std::int64_t controller_retry_after_ms_{};
};

// api to Renderer
class RenderApi {
  public:
    // can connect to Renderer?
    static Result<RenderConfiguration, int> GetRenderConfiguration(const std::string& host, int port);

    // verify security password in Renderer
    static Result<bool, int> VerifySecurityPassword(const std::string& host, int port, const std::string& safety_pwd_md5);
};

} // namespace px

#endif // PX_RENDER_API_H
