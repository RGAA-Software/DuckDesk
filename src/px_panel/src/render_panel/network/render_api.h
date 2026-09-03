//
// Created by RGAA on 11/06/2025.
//

#ifndef PX_RENDER_API_H
#define PX_RENDER_API_H

#include <cstdint>
#include <string>
#include "px_common_new/expected.h"

namespace px
{

    // Render configuration
    class RenderConfiguration {
    public:
        std::string device_id_;
        std::string relay_host_;
        std::string relay_port_{0};
    };

    class IpDirectLaunch {
    public:
        std::string stream_id_;
        std::int64_t expires_at_ms_ = 0;
    };

    // api to Renderer
    class RenderApi {
    public:
        // can connect to Renderer?
        static Result<RenderConfiguration, int> GetRenderConfiguration(const std::string& host, int port);

        // verify security password in Renderer
        static Result<bool, int> VerifySecurityPassword(const std::string& host, int port, const std::string& safety_pwd_md5);

        // Validate before launching px_client and return an opaque receipt for
        // the child signaling request. The receipt contains no password.
        static Result<IpDirectLaunch, int> PrepareIpDirectLaunch(
            const std::string& host,
            int port,
            const std::string& safety_pwd_md5,
            const std::string& client_nonce);
    };

}

#endif //PX_RENDER_API_H
