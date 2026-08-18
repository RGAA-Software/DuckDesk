//
// Created by RGAA on 17/05/2025.
//

#include "px_cms_client.h"
#include "px_cms_client_impl.h"
#include <asio2/websocket/ws_client.hpp>
#include <asio2/websocket/wss_client.hpp>
#include "render_panel/px_settings.h"

namespace px
{

    std::shared_ptr<PxCmsClient> PxCmsClient::Make(const std::shared_ptr<PxContext>& ctx,
                                                   const std::string& host,
                                                   int port,
                                                   const std::string& device_id) {
        // pick wss/ws by the cms ssl switch (from the access broadcast, default true)
        if (PxSettings::Instance()->IsCmsSslEnabled()) {
            return std::make_shared<PxCmsClientImpl<asio2::wss_client>>(ctx, host, port, device_id);
        }
        return std::make_shared<PxCmsClientImpl<asio2::ws_client>>(ctx, host, port, device_id);
    }

}
