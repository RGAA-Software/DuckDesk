//
// User-proxy WebSocket router for Render (localhost-only, single connection).
//

#ifndef GAMMARAY_WS_USER_PROXY_ROUTER_H
#define GAMMARAY_WS_USER_PROXY_ROUTER_H

#ifndef GR_USER_PROXY_ENABLED
#define GR_USER_PROXY_ENABLED 1
#endif

#include <memory>
#include <string>
#include "network/ws_router.h"

namespace asio2
{
    class http_session;
}

namespace tc
{
    class WsUserProxyRouter : public WsRouter, public std::enable_shared_from_this<WsUserProxyRouter> {
    public:
        static std::shared_ptr<WsUserProxyRouter> Make(const WsDataPtr& data);

        explicit WsUserProxyRouter(const WsDataPtr& data);

        void OnOpen(std::shared_ptr<asio2::http_session>& sess_ptr) override;
        void OnClose(std::shared_ptr<asio2::http_session>& sess_ptr) override;
        void OnMessage(std::shared_ptr<asio2::http_session>& sess_ptr, int64_t socket_fd, std::string_view data) override;
        void PostBinaryMessage(std::shared_ptr<Data> data) override;

        bool IsConnected() const;
        void ReplaceSession(std::shared_ptr<asio2::http_session>& sess_ptr);

    private:
        bool IsLocalPeer(const std::shared_ptr<asio2::http_session>& sess_ptr) const;
        void HandleRpMessage(const std::string& data);

    private:
        std::shared_ptr<asio2::http_session> active_session_;
    };
}

#endif //GAMMARAY_WS_USER_PROXY_ROUTER_H
