//
// Created by RGAA on 25/02/2025.
//

#include "ws_server.h"
#include "px_common_new/log.h"

namespace tc
{

    struct aop_log {
        bool before(http::web_request &req, http::web_response &rep) {
            asio2::ignore_unused(rep);
            return true;
        }

        bool after(std::shared_ptr<asio2::http_session> &session_ptr, http::web_request &req, http::web_response &rep) {
                    ASIO2_ASSERT(asio2::get_current_caller<std::shared_ptr<asio2::http_session>>().get() == session_ptr.get());
            asio2::ignore_unused(session_ptr, req, rep);
            return true;
        }
    };

    void WsServer::Prepare(const std::map<std::string, std::any>& vars) {
        exiting_ = false;
        http_server_ = std::make_shared<asio2::http_server>();
        auto weak_self = weak_from_this();
        http_server_->bind_disconnect([weak_self](std::shared_ptr<asio2::http_session>& sess_ptr) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            auto socket_fd = (uint64_t)sess_ptr->socket().native_handle();
            auto opt_sess = self->sessions_.Remove(socket_fd);
            if (opt_sess.has_value()) {
                opt_sess.value()->OnDisConnected();
            }
        });

        http_server_->support_websocket(true);
        ws_data_ = std::make_shared<WsData>(WsData{
            .vars_ = vars,
        });
    }

    void WsServer::Start(const std::string &ip, int port) {
        bool ret = http_server_->start(ip, port);
        LOGI("App server start result: {}", ret);
    }

    void WsServer::Exit() {
        exiting_ = true;
        if (http_server_ && http_server_->is_started()) {
            http_server_->stop_all_timers();
            http_server_->stop();
        }
        sessions_.Clear();
        http_server_.reset();
        ws_data_.reset();
    }

    void WsServer::AddHttpGetRouter(const std::string &path,
                                       std::function<void(const std::string& path, http::web_request &req, http::web_response &rep)>&& cbk) {
        auto weak_self = weak_from_this();
        http_server_->bind<http::verb::get>(path, [weak_self, path, cbk = std::move(cbk)](http::web_request &req, http::web_response &rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            cbk(path, req, rep);
        }, aop_log{});
    }

    void WsServer::AddHttpPostRouter(const std::string& path,
                                        std::function<void(const std::string& path, http::web_request &req, http::web_response &rep)>&& cbk) {
        auto weak_self = weak_from_this();
        http_server_->bind<http::verb::post>(path, [weak_self, path, cbk = std::move(cbk)](http::web_request &req, http::web_response &rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            cbk(path, req, rep);
        }, aop_log{});
    }


}
