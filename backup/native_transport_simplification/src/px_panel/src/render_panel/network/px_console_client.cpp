//
// Created by RGAA on 17/05/2025.
//

#include "px_console_client.h"
#include "px_console_client_impl.h"
#include <asio2/websocket/wss_client.hpp>

namespace px
{

    std::shared_ptr<PxConsoleClient> PxConsoleClient::Make(const std::shared_ptr<PxContext>& ctx,
                                                   const std::string& host,
                                                   int port,
                                                   const std::string& device_id) {
        return std::make_shared<PxConsoleClientImpl<asio2::wss_client>>(ctx, host, port, device_id);
    }

}
