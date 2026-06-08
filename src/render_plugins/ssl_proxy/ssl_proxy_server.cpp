//
// Created by RGAA on 9/08/2025.
//

#include "ssl_proxy_server.h"
#include <filesystem>
#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "tc_common_new/string_util.h"
#include "tc_common_new/url_helper.h"
#include "render/network/ws_data.h"
#include "render/network/wss_router.h"
#include "http_handler.h"

namespace tc
{

    static std::string kUrlMedia = "/media";
    static std::string kUrlFileTransfer = "/file/transfer";
    static std::string kApiPing = "/api/ping";
    static std::string kApiVerifySecurityPassword = "/verify/security/password";
    static std::string kApiGetRenderConfiguration = "/get/render/configuration";
    static std::string kApiPanelStreamMessage = "/panel/stream/message";
    static std::string kApiAllocLocalRtc = "/alloc/local/rtc";

    struct aop_log {
        bool before(http::web_request &req, http::web_response &rep) {
            asio2::ignore_unused(rep);
            return true;
        }

        bool after(std::shared_ptr<asio2::https_session> &session_ptr, http::web_request &req, http::web_response &rep) {
                    ASIO2_ASSERT(asio2::get_current_caller<std::shared_ptr<asio2::https_session>>().get() == session_ptr.get());
            asio2::ignore_unused(session_ptr, req, rep);
            return true;
        }
    };

    SSLProxyServer::SSLProxyServer(SSLProxyPlugin* plugin, uint16_t remote_port, uint16_t proxy_port) {
        plugin_ = plugin;
        http_handler_ = std::make_shared<HttpHandler>(plugin);
        remote_port_ = remote_port;
        proxy_port_ = proxy_port;
    }

    void SSLProxyServer::Start() {
        server_ = std::make_shared<asio2::https_server>();
        server_->bind_disconnect([=, this](std::shared_ptr<asio2::https_session>& sess_ptr) {
            auto socket_fd = (uint64_t)sess_ptr->socket().native_handle();
            //LOGI("client disconnected: {}", socket_fd);
            //if (stream_routers_.HasKey(socket_fd)) {
            //    if (auto opt_val = stream_routers_.Remove(socket_fd); opt_val.has_value()) {
            //        const auto& val = opt_val.value();
            //        NotifyMediaClientDisConnected(val->conn_id_, val->stream_id_, val->visitor_device_id_, val->created_timestamp_);
            //        LOGI("client session removed: {}", val->visitor_device_id_);
            //    }
            //    LOGI("App server media close, media router size: {}", stream_routers_.Size());
            //}
            //else if (ft_routers_.HasKey(socket_fd)) {
            //    ft_routers_.Remove(socket_fd);
            //}
        });

        server_->support_websocket(true);
        ws_data_ = std::make_shared<WsData>(WsData{
            .vars_ = {
                {"plugin",  this->plugin_},
            }
        });

        wchar_t exe_path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        auto exe_dir = StringUtil::ToUTF8(std::filesystem::path(exe_path).parent_path().wstring());
        auto pwd_file = std::format("{}/certs/password", exe_dir);
        auto pwd = (File::OpenForRead(pwd_file))->ReadAllAsString();
        server_->set_cert_file(
            "",
            std::format("{}/certs/server.crt", exe_dir),
            std::format("{}/certs/server.key", exe_dir),
            pwd);

        if (asio2::get_last_error()) {
            LOGE("load cert files failed: {}", asio2::last_error_msg());
        }
        else {
            LOGE("set cert files success.");
        }
        //server_->set_verify_mode(asio::ssl::verify_peer);

        // default
        server_->bind<http::verb::get, http::verb::post>("/", [](http::web_request& req, http::web_response& rep) {
            asio2::ignore_unused(req, rep);
            rep.fill_file("/web_client/index.html");
        }, aop_log{});

        // If no method is specified, GET and POST are both enabled by default.
        server_->bind("*", [](http::web_request& req, http::web_response& rep) {
            rep.fill_file("/web_client" + http::url_decode(req.target()));
            rep.chunked(true);
        }, aop_log{});

        // media websocket
        AddWebsocketRouter(kUrlMedia);
        AddWebsocketRouter(kUrlFileTransfer);

        // ping
        AddHttpRouter(kApiPing, [=, this](const std::string& path, http::web_request& req, http::web_response& rep) {
            http_handler_->HandlePing(req, rep);
        });

        //// verify security pwd
        //AddHttpRouter(kApiVerifySecurityPassword, [=, this](const std::string& path, http::web_request& req, http::web_response& rep) {
        //    http_handler_->HandleVerifySecurityPassword(req, rep);
        //});

        //// get render configuration
        //AddHttpRouter(kApiGetRenderConfiguration, [=, this](const std::string& path, http::web_request& req, http::web_response& rep) {
        //    http_handler_->HandleGetRenderConfiguration(req, rep);
        //});

        ////
        //AddHttpRouter(kApiPanelStreamMessage, [=, this](const std::string& path, http::web_request& req, http::web_response& rep) {
        //    http_handler_->HandlePanelStreamMessage(req, rep);
        //});

        // kApiAllocLocalRtc
        AddHttpRouter(kApiAllocLocalRtc, [=, this](const std::string& path, http::web_request& req, http::web_response& rep) {
            //http_handler_->HandleAllocLocalRtc(req, rep);
        });

        bool ret = server_->start("0.0.0.0", std::to_string(proxy_port_));
        LOGI("App server start result: {}, listen port: {}", ret, proxy_port_);
    }

    void SSLProxyServer::Exit() {

    }

    bool SSLProxyServer::IsWorking() {
        return server_ && server_->is_started();
    }

    void SSLProxyServer::AddWebsocketRouter(const std::string &path) {
        auto fn_get_socket_fd = [](std::shared_ptr<asio2::https_session> &sess_ptr) -> uint64_t {
            auto& s = sess_ptr->socket();
            return (uint64_t)s.native_handle();
        };
        server_->bind(path, websocket::listener<asio2::https_session>{}
            .on("message", [=, this](std::shared_ptr<asio2::https_session> &sess_ptr, std::string_view data) {
                auto socket_fd = fn_get_socket_fd(sess_ptr);
                //if (path == kUrlMedia) {
                //    stream_routers_.VisitAll([=](auto k, std::shared_ptr<WsStreamRouter>& router) mutable {
                //        if (socket_fd == k) {
                //            router->OnMessage(sess_ptr, socket_fd, data);
                //        }
                //    });
                //}
                //else if (path == kUrlFileTransfer) {
                //    ft_routers_.VisitAll([=](auto k, auto &router) mutable {
                //        if (socket_fd == k) {
                //            router->OnMessage(sess_ptr, socket_fd, data);
                //        }
                //    });
                //}
            })
            .on("open", [=, this](std::shared_ptr<asio2::https_session> &sess_ptr) {
                auto query = sess_ptr->get_request().get_query();
                auto params = UrlHelper::ParseQueryString(std::string(query.data(), query.size()));
                for (const auto& [k, v] : params) {
                    LOGI("query param, k: {}, v: {}", k, v);
                }
                LOGI("App server {} open, query: {}", path, query);
                bool only_audio = std::atoi(params["only_audio"].c_str()) == 1;
                std::string server_device_id;
                std::string visitor_device_id;
                std::string stream_id;
                if (params.contains("remote_device_id")) {
                    server_device_id = params["remote_device_id"];
                }
                if (params.contains("visitor_device_id")) {
                    visitor_device_id = params["visitor_device_id"];
                }
                if (params.contains("stream_id")) {
                    stream_id = params["stream_id"];
                }

                // TEST //
                if (stream_id.empty()) {
                    LOGE("!!!MUST HAVE STREAM ID!!!");
                    sess_ptr->stop();
                    return;
                }
                // TEST //

                sess_ptr->set_no_delay(true);
                auto socket_fd = fn_get_socket_fd(sess_ptr);

                //if (path == kUrlMedia) {
                //    auto router = WsStreamRouter::Make(ws_data_, only_audio, visitor_device_id, stream_id);
                //    stream_routers_.Insert(socket_fd, router);
                //    NotifyMediaClientConnected(router->conn_id_, router->stream_id_, visitor_device_id);
                //    router->OnOpen(sess_ptr);
                //}
                //else if (path == kUrlFileTransfer) {
                //    auto router = WsFileTransferRouter::Make(ws_data_, only_audio, visitor_device_id, stream_id);
                //    ft_routers_.Insert(socket_fd, router);
                //    router->OnOpen(sess_ptr);
                //}

            })
            .on("close", [=, this](std::shared_ptr<asio2::https_session> &sess_ptr) {
                auto socket_fd = fn_get_socket_fd(sess_ptr);
                LOGI("client closed: {}", socket_fd);
                //if (path == kUrlMedia) {
                //    if (auto opt_val = stream_routers_.Remove(socket_fd); opt_val.has_value()) {
                //        const auto& val = opt_val.value();
                //        NotifyMediaClientDisConnected(val->conn_id_, val->stream_id_, val->visitor_device_id_, val->created_timestamp_);
                //        LOGI("client session removed: {}", val->visitor_device_id_);
                //    }
                //}
                //else if (path == kUrlFileTransfer) {
                //    ft_routers_.Remove(socket_fd);
                //}
            })
            .on_ping([=, this](auto &sess_ptr) {

            })
            .on_pong([=, this](auto &sess_ptr) {

            })
            .on("update", [](std::shared_ptr<asio2::https_session> &sess_ptr) {
                LOGI("update");
            })
        );
    }

    void SSLProxyServer::AddHttpRouter(const std::string &path,
                                       std::function<void(const std::string& path, http::web_request& req, http::web_response& rep)>&& callback) {
        // bind it
        server_->bind<http::verb::get, http::verb::post>(path, [=, this](http::web_request &req, http::web_response &rep) {
            callback(path, req, rep);
        }, aop_log{}); //, http::enable_cache
    }

}