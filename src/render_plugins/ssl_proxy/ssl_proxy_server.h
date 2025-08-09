//
// Created by RGAA on 9/08/2025.
//

#ifndef GAMMARAYPREMIUM_SSL_PROXY_SERVER_H
#define GAMMARAYPREMIUM_SSL_PROXY_SERVER_H

#include <cstdint>
#include <functional>
#include <asio2/asio2.hpp>

namespace tc
{

    class WsData;
    class HttpHandler;
    class SSLProxyPlugin;

    class SSLProxyServer {
    public:
        SSLProxyServer(SSLProxyPlugin* plugin, uint16_t remote_port, uint16_t proxy_port);
        void Start();
        void Exit();
        bool IsWorking();

    private:
        void AddWebsocketRouter(const std::string& path);
        void AddHttpRouter(const std::string& path,
                           std::function<void(const std::string& path, http::web_request& req, http::web_response& rep)>&& callback);

    private:
        SSLProxyPlugin* plugin_ = nullptr;
        std::shared_ptr<WsData> ws_data_ = nullptr;
        std::shared_ptr<asio2::https_server> server_ = nullptr;
        std::shared_ptr<HttpHandler> http_handler_ = nullptr;
        uint16_t remote_port_ = 0;
        uint16_t proxy_port_ = 0;
    };

}

#endif //GAMMARAYPREMIUM_SSL_PROXY_SERVER_H
