//
// Created by RGAA on 17/05/2025.
//

#ifndef PX_CLIENT_CONSOLE_CLIENT_H
#define PX_CLIENT_CONSOLE_CLIENT_H

#include <memory>
#include <string>

namespace px
{
    class ClientContext;
    class ThunderSdk;

    // Console connection client. The legacy ssl argument remains ABI-compatible,
    // but Console transport is always WSS.
    class CtConsoleClient {
    public:
        virtual ~CtConsoleClient() = default;
        virtual void Start() = 0;
        virtual void Exit() = 0;

        static std::shared_ptr<CtConsoleClient> Make(const std::shared_ptr<ThunderSdk>& sdk,
                                                  const std::shared_ptr<ClientContext>& ctx,
                                                  const std::string& host,
                                                  int port,
                                                  const std::string& device_id,
                                                  const std::string& remote_device_id,
                                                  const std::string& remote_device_ip,
                                                  const std::string& appkey,
                                                  bool ssl);
    };

}

#endif //PX_CLIENT_CONSOLE_CLIENT_H
