//
// Created by RGAA on 2024-03-30.
//

#include "ws_panel_server.h"
#include <QApplication>
#include "apis.h"
#include "http_handler.h"
#include "records_http_handler.h"
#include "px_message.pb.h"
#include "render_panel/px_settings.h"
#include <nlohmann/json.hpp>
#include "px_common_new/http_client.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "px_common_new/privacy_log.h"
#include "px_common_new/data.h"
#include "px_common_new/file.h"
#include "px_render_panel_message.pb.h"
#include "px_client_panel_message.pb.h"
#include "render_panel/px_context.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/px_application.h"
#include "render_panel/px_render_msg_processor.h"
#include "render_panel/database/px_database.h"
#include "render_panel/database/visit_record.h"
#include "render_panel/database/visit_record_operator.h"
#include "render_panel/database/file_transfer_record.h"
#include "render_panel/database/file_transfer_record_operator.h"
#include "px_message_new/rp_proto_converter.h"
#include "px_common_new/url_helper.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/weak_callback.h"
#include "px_qt_widget/translator/px_translator.h"
#include "render_panel/companion/panel_companion.h"
#include "render_panel/px_statistics.h"
#include "render_panel/devices/px_device_manager.h"
#include "skin/interface/skin_interface.h"
#include "render_panel/console/px_event_manager.h"

namespace px
{

    static std::string kUrlPanel = "/panel";
    static std::string kUrlPanelRenderer = "/panel/renderer";
    static std::string kUrlSysInfo = "/sys/info";

    namespace {

    bool IsInternalWebsocketPath(const std::string& path) {
        return path == kUrlPanelRenderer || path == kUrlSysInfo;
    }

    bool IsLoopbackPeer(const std::string& address) {
        asio::error_code ec;
        const auto parsed = asio::ip::make_address(address, ec);
        if (ec) {
            return false;
        }
        if (parsed.is_loopback()) {
            return true;
        }
        if (parsed.is_v6()) {
            const auto ipv6 = parsed.to_v6();
            if (ipv6.is_v4_mapped()) {
                return ipv6.to_v4().is_loopback();
            }
        }
        return false;
    }

    }  // namespace

    // report visit info to console
    static const std::string kUrlVisitRecord  = "/api/v1/record/upload_visit_info";

    static const std::string kUrlUpdateVisitRecord = "/api/v1/record/update_visit_info";

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

    std::shared_ptr<WsPanelServer> WsPanelServer::Make(const std::shared_ptr<PxApplication>& app) {
        return std::make_shared<WsPanelServer>(app);
    }

    WsPanelServer::WsPanelServer(const std::shared_ptr<PxApplication>& app) {
        app_ = app;
        stat_ = PxStatistics::Instance();
        context_ = app_->GetContext();
        http_handler_ = std::make_shared<HttpHandler>(app_);
        records_http_handler_ = std::make_shared<RecordsHttpHandler>(app_);
        visit_record_op_ = context_->GetDatabase()->GetVisitRecordOp();
        ft_record_op_ = context_->GetDatabase()->GetFileTransferRecordOp();

    }

    void WsPanelServer::Start() {
        if (exiting_ || server_) {
            return;
        }
        auto weak_self = weak_from_this();
        msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kControl);
        state_msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kState);
        msg_listener_->Listen<MsgSecurityPasswordUpdated>([weak_self](const MsgSecurityPasswordUpdated& msg) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->RpSyncPanelInfo();
                    }
                });
            }
        });

        state_msg_listener_->Listen<MsgGrTimer1S>([weak_self](const MsgGrTimer1S&) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->RpSyncPanelInfo();
                    }
                });
                if (!self->audit_flush_in_progress_.exchange(true)) {
                    self->context_->PostDBTask([weak_self]() {
                        if (auto self = weak_self.lock(); self && !self->exiting_) {
                            self->FlushAuditOutbox();
                        }
                    });
                }
            }
        });

        state_msg_listener_->Listen<MsgGrTimer5S>([weak_self](const MsgGrTimer5S&) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_ && self->panel_sessions_.Size() > 0) {
                        static_cast<void>(
                            self->app_->GetDeviceManager()->UpdateUsedTime(5000));
                    }
                });
            }
        });

        state_msg_listener_->Listen<MsgHWInfo>([weak_self](const MsgHWInfo& msg) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                if (msg.sys_info_->networks_.empty()) {
                    return;
                }
                const auto& def_ethernet = msg.sys_info_->networks_[0];
                self->max_transmit_speed_ = def_ethernet.max_transmit_speed_;
                self->max_receive_speed_ = def_ethernet.max_receive_speed_;
            }
        });

        server_ = std::make_shared<asio2::http_server>();
        server_->bind_disconnect([weak_self](std::shared_ptr<asio2::http_session>& sess_ptr) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            auto socket_fd = (uint64_t)sess_ptr->socket().native_handle();
            if (auto removed = self->panel_sessions_.Remove(socket_fd); removed.has_value()) {
                self->PanelSocketClosed(removed.value());
                LOGI("Panel;client disconnected: {}", socket_fd);
                LOGI("Panel;App server media close, media router size: {}", self->panel_sessions_.Size());
            }
            if (auto removed = self->renderer_sessions_.Remove(socket_fd); removed.has_value()) {
                self->RendererSocketClosed(removed.value());
                LOGI("Renderer;client disconnected: {}", socket_fd);
                LOGI("Renderer;App server media close, media router size: {}", self->panel_sessions_.Size());
            }
        });

        server_->support_websocket(true);
        ws_data_ = std::make_shared<WsData>(WsData{
            .vars_ = {
                {"app",  this->app_},
            }
        });

        //auto exe_dir = qApp->applicationDirPath().toStdString();
        //auto pwd_file = std::format("{}/certs/password", exe_dir);
        //auto pwd = px::File::OpenForRead(pwd_file)->ReadAllAsString();
        //server_->set_cert_file(
        //    "",
        //    std::format("{}/certs/server.crt", exe_dir),
        //    std::format("{}/certs/server.key", exe_dir),
        //    pwd
        //);

        //if (asio2::get_last_error()) {
        //    LOGE("load cert files failed: {}", asio2::last_error_msg());
        //}
        //else {
        //    LOGE("set cert files success.");
        //}

        ////  | asio::ssl::verify_fail_if_no_peer_cert
        //server_->set_verify_mode(asio::ssl::verify_peer);

        // response a "Pong" for checking server state
        AddHttpGetRouter(kPathPing, MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandlePing(req, rep);
            }
        }));

        // response the information that equals to the QR Code
        AddHttpGetRouter(kPathSimpleInfo, MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleSimpleInfo(req, rep);
            }
        }));

        // response all apps that we found in system and added by user
        AddHttpGetRouter(kPathGames, MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleGames(req, rep);
            }
        }));

        // start game
        AddHttpPostRouter(kPathGameStart, MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleGameStart(req, rep);
            }
        }));

        // stop game
        AddHttpPostRouter(kPathGameStop, MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleGameStop(req, rep);
            }
        }));

        // running games
        AddHttpGetRouter(kPathRunningGames, MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleRunningGames(req, rep);
            }
        }));

        // stop the px_render.exe
        AddHttpGetRouter(kPathStopServer, MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleStopServer(req, rep);
            }
        }));

        // all running processes in th PC, equals the process list in TaskManager
        AddHttpGetRouter(kPathAllRunningProcesses, MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleAllRunningProcesses(req, rep);
            }
        }));

        // kill a process by pid
        AddHttpPostRouter(kPathKillProcess, MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleKillProcess(req, rep);
            }
        }));

        // res
        AddHttpGetRouter("/res/*", MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleResourcesFile(req, rep);
            }
        }));

        // render records: file list (docs/console_render_records_view_design.md 5.1)
        AddHttpGetRouter("/records", MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->records_http_handler_->HandleRecordsList(req, rep);
            }
        }));

        // render records: dir/space info (lightweight, for web topology probing)
        AddHttpGetRouter("/records/info", MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->records_http_handler_->HandleRecordsInfo(req, rep);
            }
        }));

        // render records: file download with manual Range support
        AddHttpGetRouter("/records/*", MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->records_http_handler_->HandleRecordFile(req, rep);
            }
        }));

        // cache
        AddHttpGetRouter("/steam/cache/*", MakeWeakVoidCallback(weak_self,
            [](const auto& self, const auto&, auto& req, auto& rep) {
            if (!self->exiting_) {
                self->http_handler_->HandleSteamCacheFile(req, rep);
            }
        }));

        // default
        server_->bind<http::verb::get, http::verb::post>("/", [](http::web_request& req, http::web_response& rep) {
            asio2::ignore_unused(req, rep);
            rep.fill_file("/web/index.html");
        }, aop_log{});

        // If no method is specified, GET and POST are both enabled by default.
        server_->bind("*", [](http::web_request& req, http::web_response& rep) {
            rep.fill_file("/web" + http::url_decode(req.target()));
            rep.chunked(true);
        }, aop_log{});

        // panel
        AddWebsocketRouter(kUrlPanel);
        // panel/renderer
        AddWebsocketRouter(kUrlPanelRenderer);
        // sys info
        AddWebsocketRouter(kUrlSysInfo);

        const auto panel_port = PxSettings::Instance()->GetPanelServerPort();
        bool ret = server_->start("0.0.0.0", panel_port);
        LOGI("App server start result: {}, port: {}", ret, panel_port);

        context_->PostDBTask([weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->visit_record_op_->FlushPendingRecords();
                self->ft_record_op_->FlushPendingRecords();
                self->ScanAndFixUnclosedRecords();
            }
        });
    }

    void WsPanelServer::Exit() {
        if (exiting_.exchange(true)) {
            return;
        }
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        if (state_msg_listener_) {
            state_msg_listener_->UnListenAll();
            state_msg_listener_.reset();
        }
        if (server_) {
            server_->stop_all_timers();
            server_->stop();
            server_.reset();
        }
        panel_sessions_.Clear();
        renderer_sessions_.Clear();
        sys_info_sess_.reset();
        records_http_handler_.reset();
        http_handler_.reset();
        ft_record_op_.reset();
        visit_record_op_.reset();
        stat_.reset();
        context_.reset();
        app_.reset();
    }

    WsPanelServer::~WsPanelServer() {
        Exit();
    }

    void WsPanelServer::AddWebsocketRouter(const std::string &path) {
        auto weak_self = weak_from_this();
        auto fn_get_socket_fd = [](std::shared_ptr<asio2::http_session> &sess_ptr) -> uint64_t {
            auto& s = sess_ptr->socket();
            return (uint64_t)s.native_handle();
        };
        server_->bind(path, websocket::listener<asio2::http_session>{}
            .on("message", [weak_self, path, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr, std::string_view data) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                if (IsInternalWebsocketPath(path) && !IsLoopbackPeer(sess_ptr->remote_address())) {
                    LOGW("Rejecting non-loopback message on internal websocket path: {}, peer: {}",
                         path, sess_ptr->remote_address());
                    sess_ptr->stop();
                    return;
                }
                auto socket_fd = fn_get_socket_fd(sess_ptr);
                if (path == kUrlPanel) {
                    self->ParsePanelMessage(socket_fd, data);
                }
                else if (path == kUrlPanelRenderer) {
                    self->ParseRendererMessage(socket_fd, data);
                }
                else if (path == kUrlSysInfo) {
                    if (self->sys_info_sess_) {
                        self->ParseSysInfoMessage(socket_fd, data);
                    }
                }
            })
            .on("open", [weak_self, path, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                if (IsInternalWebsocketPath(path) && !IsLoopbackPeer(sess_ptr->remote_address())) {
                    LOGW("Rejecting non-loopback connection to internal websocket path: {}, peer: {}",
                         path, sess_ptr->remote_address());
                    sess_ptr->stop();
                    return;
                }
                LOGI("App server {} open", path);
                sess_ptr->ws_stream().binary(true);
                sess_ptr->set_no_delay(true);
                auto socket_fd = fn_get_socket_fd(sess_ptr);

                auto query = sess_ptr->get_request().get_query();
                auto params = UrlHelper::ParseQueryString(std::string(query.data(), query.size()));

                if (path == kUrlPanel) {
                    for (const auto& [k, v] : params) {
                        LOGI("query param, k: {}, v: {}", k, v);
                    }
                    LOGI("App server {} open, query: {}", path, query);
                    std::string stream_id;
                    if (params.contains("stream_id")) {
                        stream_id = params["stream_id"];
                    }

                    auto ws_sess = std::make_shared<WSSession>();
                    ws_sess->socket_fd_ = socket_fd;
                    ws_sess->session_ = sess_ptr;
                    ws_sess->stream_id_ = stream_id;
                    ws_sess->audit_registered_ = !stream_id.empty();
                    self->panel_sessions_.Insert(socket_fd, ws_sess);
                    if (ws_sess->audit_registered_) {
                        self->PanelSocketOpened(stream_id);
                    }
                    LOGI("Panel;client connect : {}", socket_fd);
                }
                else if (path == kUrlPanelRenderer) {
                    auto ws_sess = std::make_shared<WSSession>();
                    ws_sess->socket_fd_ = socket_fd;
                    ws_sess->session_ = sess_ptr;
                    ws_sess->stream_id_ = params.contains("instance_id")
                        ? params["instance_id"]
                        : std::format("legacy-{}", socket_fd);
                    self->renderer_sessions_.Insert(socket_fd, ws_sess);
                    self->RendererSocketOpened(ws_sess->stream_id_);
                    LOGI("Renderer;client connect : {}", socket_fd);

                    sess_ptr->post_queued_event([weak_self]() {
                        if (auto self = weak_self.lock(); self && !self->exiting_) {
                            self->RpSyncPanelInfo();
                        }
                    });
                }
                else if (path == kUrlSysInfo) {
                    auto sess = std::make_shared<WSSession>();
                    sess->socket_fd_ = socket_fd;
                    sess->session_ = sess_ptr;
                    self->sys_info_sess_ = sess;
                }
            })
            .on("close", [weak_self, path, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto socket_fd = fn_get_socket_fd(sess_ptr);
                if (path == kUrlPanel) {
                    if (auto removed = self->panel_sessions_.Remove(socket_fd); removed.has_value()) {
                        self->PanelSocketClosed(removed.value());
                    }
                }
                else if (path == kUrlPanelRenderer) {
                    if (auto removed = self->renderer_sessions_.Remove(socket_fd); removed.has_value()) {
                        self->RendererSocketClosed(removed.value());
                    }
                }
                else if (path == kUrlSysInfo) {
                    if (self->sys_info_sess_) {
                        self->sys_info_sess_.reset();
                    }
                }
            })
            .on_ping([weak_self](auto &sess_ptr) {

            })
            .on_pong([weak_self](auto &sess_ptr) {

            })
        );
    }

    bool WsPanelServer::IsAlive() {
        return server_ && server_->is_started();
    }

    void WsPanelServer::AddHttpGetRouter(const std::string &path,
        std::function<void(const std::string& path, http::web_request &req, http::web_response &rep)>&& cbk) {
        server_->bind<http::verb::get>(path, [path, callback = std::move(cbk)](http::web_request &req, http::web_response &rep) {
            callback(path, req, rep);
        }, aop_log{});
    }

    void WsPanelServer::AddHttpPostRouter(const std::string& path,
        std::function<void(const std::string& path, http::web_request &req, http::web_response &rep)>&& cbk) {
        server_->bind<http::verb::post>(path, [path, callback = std::move(cbk)](http::web_request &req, http::web_response &rep) {
            callback(path, req, rep);
        }, aop_log{});
    }

    void WsPanelServer::PostPanelMessage(const std::string& msg, bool only_inner) {
        panel_sessions_.VisitAll([msg, only_inner](uint64_t, std::shared_ptr<WSSession>& sess) {
            if (only_inner && sess->session_type_ != pxcp::CpSessionType::kInnerServer) {
                return;
            }
            if (sess->session_) {
                sess->session_->async_send(msg);
            }
        });
    }

    bool WsPanelServer::PostPanelMessageToStream(const std::string& stream_id, const std::string& msg) {
        std::atomic_bool delivered {false};
        panel_sessions_.VisitAll([&](uint64_t, std::shared_ptr<WSSession>& sess) {
            if (!sess || sess->stream_id_ != stream_id || !sess->session_
                || sess->session_type_ != pxcp::CpSessionType::kWindowsClient) {
                return;
            }
            sess->session_->async_send(msg);
            delivered.store(true);
        });
        return delivered.load();
    }

    bool WsPanelServer::ParsePanelMessage(uint64_t socket_fd, std::string_view msg) {
        auto proto_msg = std::make_shared<pxcp::CpMessage>();
        if (!proto_msg->ParseFromArray(msg.data(), msg.size())) {
            return false;
        }
        if (proto_msg->type() == pxcp::CpMessageType::kCpHello) {
            auto hello = proto_msg->hello();
            const auto weak_self = weak_from_this();
            panel_sessions_.VisitAll([weak_self, socket_fd, hello, proto_msg](uint64_t, std::shared_ptr<WSSession>& v) {
                if (v->socket_fd_ == socket_fd) {
                    v->session_type_ = hello.type();
                    if (!v->audit_registered_ && !proto_msg->stream_id().empty()) {
                        v->stream_id_ = proto_msg->stream_id();
                        v->audit_registered_ = true;
                        if (const auto self = weak_self.lock(); self && !self->exiting_) {
                            self->PanelSocketOpened(v->stream_id_);
                        }
                    }
                    LOGI("Update session type: {} for socket: {}", v->session_type_, socket_fd);
                }
            });

            context_->SendAppMessage(MsgClientConnectedPanel {
                .stream_id_ = proto_msg->stream_id(),
                .sess_type_ = hello.type(),
            });
        }
        else if (proto_msg->type() == pxcp::CpMessageType::kCpTransportConnected
                 && !proto_msg->stream_id().empty()) {
            LOGI("Client remote transport connected: {}", proto_msg->stream_id());
            context_->SendAppMessage(MsgClientTransportConnectedPanel {
                .stream_id_ = proto_msg->stream_id(),
            });
        }
        else if (proto_msg->type() == pxcp::CpMessageType::kCpHeartBeat) {
            auto hb = proto_msg->heartbeat();
            //LOGI("HB: stream id: {} remote desktop: {} os: {}", proto_msg->stream_id(), hb.remote_device_desktop_name(), hb.remote_os_name());
            if (proto_msg->stream_id().empty() || hb.remote_device_desktop_name().empty() || hb.remote_os_name().empty()) {
                return false;
            }

            context_->SendAppMessage(MsgRemotePeerInfo {
                .stream_id_ = proto_msg->stream_id(),
                .desktop_name_ = hb.remote_device_desktop_name(),
                .os_version_ = hb.remote_os_name(),
            });
        }
        else if (proto_msg->type() == pxcp::CpMessageType::kCpRtcIceRestartRequest
                 && !proto_msg->stream_id().empty()) {
            LOGW("Standard RTC client requested ICE restart: {}", proto_msg->stream_id());
            context_->SendAppMessage(MsgClientRtcIceRestartRequested {
                .stream_id_ = proto_msg->stream_id(),
            });
        }
        else if (proto_msg->type() == pxcp::kCpFileTransferBegin) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg, socket_fd]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto sub = proto_msg->ft_transfer_beg();
                auto ips = self->context_->GetIps();
                std::string ip_address;
                if (!ips.empty()) {
                    ip_address = ips[0].ip_addr_;
                }
                const auto device_id = PxSettings::Instance()->GetDeviceId();

                auto record = std::make_shared<FileTransferRecord>(FileTransferRecord{
                    .the_file_id_ = sub.the_file_id(),
                    .begin_ = sub.begin_timestamp(),
                    .end_ = 0,
                    .visitor_device_ = device_id.empty() ? ip_address : device_id,
                    .target_device_ = sub.remote_device_id(),
                    .direction_ = sub.direction(),
                    .file_detail_ = sub.file_detail(),
                });

                self->ft_record_op_->InsertFileTransferRecord(record);
                self->TrackPanelTransfer(socket_fd, sub.the_file_id(), true);
                self->NotifyInsertFileTransferRecordToConsole(record);
            });
        }
        else if (proto_msg->type() == pxcp::kCpFileTransferEnd) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg, socket_fd]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto sub = proto_msg->ft_transfer_end();
                self->TrackPanelTransfer(socket_fd, sub.the_file_id(), false);
                const auto status = sub.status().empty()
                    ? (sub.success() ? "succeeded" : "failed")
                    : sub.status();
                const auto reason = sub.end_reason().empty()
                    ? (sub.success() ? "completed" : "transfer_failed")
                    : sub.end_reason();
                self->ft_record_op_->UpdateFileTransferRecord(
                    sub.the_file_id(), sub.end_timestamp(), sub.success(), status, reason);

                if (const auto opt = self->ft_record_op_->GetFileTransferRecordByFileId(sub.the_file_id()); opt.has_value()) {
                    self->NotifyUpdateFileTransferRecordToConsole(opt.value());
                }
            });
        }
        return true;
    }

    void WsPanelServer::RpSyncPanelInfo() {
        pxrp::RpMessage m;
        m.set_type(pxrp::RpMessageType::kSyncPanelInfo);
        auto sub = m.mutable_sync_panel_info();
        sub->set_device_id(PxSettings::Instance()->GetDeviceId());
        sub->set_device_random_pwd(PxSettings::Instance()->GetDeviceRandomPwd());
        sub->set_device_safety_pwd(PxSettings::Instance()->GetDeviceSecurityPwd());
        sub->set_relay_host(PxSettings::Instance()->GetRelayServerHost());
        sub->set_relay_port(std::to_string(PxSettings::Instance()->GetRelayServerPort()));
        sub->set_can_be_operated(PxSettings::Instance()->IsBeingOperatedEnabled());
        sub->set_relay_enabled(PxSettings::Instance()->IsRelayEnabled());
        sub->set_language((int)tcTrMgr()->GetSelectedLanguage());
        sub->set_file_transfer_enabled(PxSettings::Instance()->IsFileTransferEnabled());
        sub->set_audio_enabled(PxSettings::Instance()->IsCaptureAudioEnabled());
        sub->set_appkey(grApp->GetAppkey());
        sub->set_max_transmit_speed(this->max_transmit_speed_);
        sub->set_max_receive_speed(this->max_receive_speed_);
        if (auto pc = grApp->GetCompanion(); pc && pc->GetAuth()) {
            sub->set_role(static_cast<int>(pc->GetAuth()->role_));
        }
        else {
            sub->set_role(1);
        }
        PostRendererMessage(px::RpProtoAsData(&m));
    }

    // to /panel/renderer socket
    void WsPanelServer::PostRendererMessage(std::shared_ptr<Data> msg) {
        renderer_sessions_.VisitAll([=](uint64_t fd, std::shared_ptr<WSSession>& sess) {
            if (sess->session_) {
                sess->session_->async_send(msg->AsString());
            }
        });
    }

    // parse /panel/renderer socket
    void WsPanelServer::ParseRendererMessage(uint64_t socket_fd, std::string_view msg) {
        auto proto_msg = std::make_shared<pxrp::RpMessage>();
        if (!proto_msg->ParseFromArray(msg.data(), msg.size())) {
            LOGE("Parse binary message failed.");
            return;
        }
        if (proto_msg->type() == pxrp::kRpCaptureStatistics) {
            auto statistics = std::make_shared<pxrp::RpCaptureStatistics>();
            statistics->CopyFrom(proto_msg->capture_statistics());
            context_->SendAppMessage(MsgCaptureStatistics{
                .msg_ = proto_msg,
                .statistics_ = statistics,
            });
        }
        else if (proto_msg->type() == pxrp::kRpServerAudioSpectrum) {
            //auto spectrum = proto_msg->renderer_audio_spectrum();
            auto spectrum = std::make_shared<pxrp::RpServerAudioSpectrum>();
            spectrum->CopyFrom(proto_msg->renderer_audio_spectrum());
            context_->SendAppMessage(MsgServerAudioSpectrum {
                .msg_ = proto_msg,
                .spectrum_ = spectrum,
            });
        }
        else if (proto_msg->type() == pxrp::kRpRestartServer) {
            context_->SendAppMessage(AppMsgRestartServer {});
        }
        else if (proto_msg->type() == pxrp::kRpPluginsInfo) {
            auto plugins_info = std::make_shared<pxrp::RpPluginsInfo>();
            plugins_info->CopyFrom(proto_msg->plugins_info());
            context_->SendAppMessage(MsgPluginsInfo {
                .plugins_info_ = plugins_info,
            });
        }
        else if (proto_msg->type() == pxrp::kRpClientConnected) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg, socket_fd]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto ips = self->context_->GetIps();
                std::string ip_address;
                if (!ips.empty()) {
                    ip_address = ips[0].ip_addr_;
                }
                const auto device_id = PxSettings::Instance()->GetDeviceId();
                auto sub = proto_msg->client_connected();
                if (sub.visitor_device_id().empty()) {
                    return;
                }
                auto record = std::make_shared<VisitRecord>(VisitRecord{
                    .conn_id_ = sub.conn_id(),
                    .stream_id_ = sub.stream_id(),
                    .conn_type_ = sub.conn_type(),
                    .begin_ = sub.begin_timestamp(),
                    .end_ = 0,
                    .duration_ = 0,
                    .visitor_device_ = sub.visitor_device_id(),
                    .target_device_ = device_id.empty() ? ip_address : device_id,
                });
                self->visit_record_op_->InsertVisitRecord(record);
                self->TrackRendererVisit(socket_fd, sub.conn_id(), true);
                // notify console
                self->NotifyInsertVisitRecordToConsole(record);
            });
        }
        else if (proto_msg->type() == pxrp::kRpClientDisConnected) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg, socket_fd]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto sub = proto_msg->client_disconnected();
                self->TrackRendererVisit(socket_fd, sub.conn_id(), false);
                self->visit_record_op_->UpdateVisitRecord(sub.conn_id(), sub.end_timestamp(), sub.duration());
                if (const auto record = self->visit_record_op_->GetVisitRecordConnId(sub.conn_id()); record.has_value()) {
                    self->NotifyUpdateVisitRecordToConsole(record.value());
                }
            });
            context_->SendAppMessage(MsgOneClientDisconnect{});
        }
        else if (proto_msg->type() == pxrp::kRpFileTransferBegin) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg, socket_fd]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto sub = proto_msg->ft_begin();
                auto ips = self->context_->GetIps();
                std::string ip_address;
                if (!ips.empty()) {
                    ip_address = ips[0].ip_addr_;
                }
                const auto device_id = PxSettings::Instance()->GetDeviceId();

                auto record = std::make_shared<FileTransferRecord>(FileTransferRecord {
                    .the_file_id_ = sub.the_file_id(),
                    .begin_ = sub.begin_timestamp(),
                    .end_ = 0,
                    .visitor_device_ = sub.visitor_device_id(),
                    .target_device_ = device_id.empty() ? ip_address : device_id,
                    .direction_ = sub.direction(),
                    .file_detail_ = sub.file_detail(),
                });
                self->ft_record_op_->InsertFileTransferRecord(record);
                self->TrackRendererTransfer(socket_fd, sub.the_file_id(), true);
                self->NotifyInsertFileTransferRecordToConsole(record);
            });
        }
        else if (proto_msg->type() == pxrp::kRpFileTransferEnd) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg, socket_fd]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto sub = proto_msg->ft_end();
                self->TrackRendererTransfer(socket_fd, sub.the_file_id(), false);
                const auto status = sub.status().empty()
                    ? (sub.success() ? "succeeded" : "failed")
                    : sub.status();
                const auto reason = sub.end_reason().empty()
                    ? (sub.success() ? "completed" : "transfer_failed")
                    : sub.end_reason();
                self->ft_record_op_->UpdateFileTransferRecord(
                    sub.the_file_id(), sub.end_timestamp(), sub.success(), status, reason);

                if (const auto opt = self->ft_record_op_->GetFileTransferRecordByFileId(sub.the_file_id()); opt.has_value()) {
                    self->NotifyUpdateFileTransferRecordToConsole(opt.value());
                }
            });
        }
        else if (proto_msg->type() == pxrp::kRpRawRenderMessage) {
            // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
#if 0
            auto sub = proto_msg->raw_render_msg();
            auto rd_proto_msg = std::make_shared<px::Message>();
            if (!rd_proto_msg->ParseFromString(sub.msg())) {
                LOGE("kRpRawRenderMessage parse failed");
                return;
            }
            auto processor = app_->GetRenderMsgProcessor();
            processor->OnMessage(rd_proto_msg);
#endif
        }
        else if (proto_msg->type() == pxrp::kRpRelayAlive) {
            auto sub = proto_msg->relay_alive();
            // 以 panel 实际收到消息的时刻为准:消息能到达即代表 relay 链路活着,
            // render 侧的时间戳可能因任务队列排队而滞后数秒,会误判为离线。
            stat_->UpdateRelayAlive(sub.device_id(), (int64_t)TimeUtil::GetCurrentTimestamp());
        }
        else if (proto_msg->type() == pxrp::kRpMonitorChanged) {
            context_->SendAppMessage(MsgMonitorChanged{});
        }
        else if (proto_msg->type() == pxrp::kRpVoiceCallConsentRequest) {
            const auto& sub = proto_msg->voice_call_consent_request();
            LOGI("[VoiceCall] px_panel received consent request, call={}, stream={}, request={}",
                 PrivacyLogId(sub.call_id()), sub.stream_id(), sub.request_id());
            context_->SendAppMessage(MsgPanelVoiceCallConsentRequest{
                .visitor_device_id_ = sub.visitor_device_id(),
                .stream_id_ = sub.stream_id(),
                .call_id_ = sub.call_id(),
                .request_id_ = sub.request_id(),
                .expires_at_unix_ms_ = sub.expires_at_unix_ms(),
                .protocol_version_ = sub.protocol_version(),
            });
        }
        else if (proto_msg->type() == pxrp::kRpVoiceCallConsentCancel) {
            const auto& sub = proto_msg->voice_call_consent_cancel();
            LOGI("[VoiceCall] px_panel received consent cancel, call={}, stream={}, request={}, reason={}",
                 PrivacyLogId(sub.call_id()), sub.stream_id(), sub.request_id(), sub.reason());
            context_->SendAppMessage(MsgPanelVoiceCallConsentCancel{
                .stream_id_ = sub.stream_id(),
                .call_id_ = sub.call_id(),
                .request_id_ = sub.request_id(),
                .reason_ = sub.reason(),
            });
        }
    }

    void WsPanelServer::ParseSysInfoMessage(uint64_t socket_fd, std::string_view msg) {
        auto companion = app_->GetCompanion();
        if (!companion) {
            return;
        }

        std::string m = std::string(msg.data(), msg.size());
        auto sys_info = companion->ParseHardwareInfo(m);
        if (!sys_info) {
            return;
        }

        context_->SendAppMessage(MsgHWInfo {
            .sys_info_ = sys_info,
        });

        // to render
        {
            pxrp::RpMessage rp_msg;
            rp_msg.set_type(pxrp::RpMessageType::kRpHardwareInfo);
            auto sub = rp_msg.mutable_hw_info();
            sub->set_json_msg(sys_info->raw_json_msg_);
            sub->set_current_cpu_freq(companion->GetCurrentCpuFrequency());
            PostRendererMessage(px::RpProtoAsData(&rp_msg));
        }

        // notify event if needed
        {
            auto weak_self = weak_from_this();
            std::call_once(notify_event_flag_, [weak_self, sys_info]() {
                if (auto self = weak_self.lock(); self && !self->exiting_) {
                    self->context_->PostTask([weak_self, sys_info]() {
                        if (auto self = weak_self.lock(); self && !self->exiting_) {
                            self->NotifyEventIfNeeded(sys_info);
                        }
                    });
                }
            });

            notify_event_count_++;
            if (notify_event_count_ >= 60) {
                notify_event_count_ = 0;
                context_->PostTask([weak_self, sys_info]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->NotifyEventIfNeeded(sys_info);
                    }
                });
            }
        }
    }

    void WsPanelServer::ScanAndFixUnclosedRecords() {
        auto db = context_->GetDatabase();
        if (!db || !db->IsReady()) {
            return;
        }

        const auto now = TimeUtil::GetCurrentTimestamp();
        // This runs before the new render process can create records, therefore every
        // open row belongs to a previous process lifetime and must be finalized.
        const auto cutoff = now + 1;

        auto visits = db->ScanUnclosedVisitRecords(cutoff);
        for (auto& r : visits) {
            r->end_ = now;
            r->duration_ = std::max<int64_t>(0, now - r->begin_);
            r->status_ = "aborted";
            r->end_reason_ = "panel_restart_recovery";
            r->recovered_ = true;
            visit_record_op_->InsertVisitRecord(r);
            NotifyInsertVisitRecordToConsole(r);
            NotifyUpdateVisitRecordToConsole(r);
        }

        auto transfers = db->ScanUnclosedFileTransferRecords(cutoff);
        for (auto& r : transfers) {
            r->end_ = now;
            r->success_ = false;
            r->duration_ = std::max<int64_t>(0, now - r->begin_);
            r->status_ = "aborted";
            r->end_reason_ = "panel_restart_recovery";
            r->recovered_ = true;
            ft_record_op_->InsertFileTransferRecord(r);
            NotifyInsertFileTransferRecordToConsole(r);
            NotifyUpdateFileTransferRecordToConsole(r);
        }

        LOGI("ScanAndFixUnclosedRecords: fixed {} visit(s), {} file transfer(s)", visits.size(), transfers.size());
    }

    void WsPanelServer::NotifyInsertVisitRecordToConsole(const std::shared_ptr<VisitRecord> record) {
        if (!record || record->conn_id_.empty()) {
            return;
        }
        auto db = context_->GetDatabase();
        if (!db->EnqueueAuditOutbox("visit:" + record->conn_id_ + ":begin", kUrlVisitRecord,
                                    record->AsJson2(), TimeUtil::GetCurrentTimestamp())) {
            LOGE("Queue insert visit audit failed: {}", record->conn_id_);
        }
    }

    void WsPanelServer::NotifyUpdateVisitRecordToConsole(const std::shared_ptr<VisitRecord> record) {
        if (!record || record->conn_id_.empty()) {
            return;
        }
        auto db = context_->GetDatabase();
        if (!db->EnqueueAuditOutbox("visit:" + record->conn_id_ + ":end", kUrlUpdateVisitRecord,
                                    record->AsUpdateJson(), TimeUtil::GetCurrentTimestamp())) {
            LOGE("Queue update visit audit failed: {}", record->conn_id_);
        }
    }

    void WsPanelServer::NotifyInsertFileTransferRecordToConsole(const std::shared_ptr<FileTransferRecord> record) {
        if (!record || record->the_file_id_.empty()) {
            return;
        }
        auto db = context_->GetDatabase();
        if (!db->EnqueueAuditOutbox("file:" + record->the_file_id_ + ":begin",
                                    FileTransferRecord::kUrlInsertFileTransferRecord,
                                    record->AsJson2(), TimeUtil::GetCurrentTimestamp())) {
            LOGE("Queue insert file transfer audit failed: {}", record->the_file_id_);
        }
    }

    void WsPanelServer::NotifyUpdateFileTransferRecordToConsole(const std::shared_ptr<FileTransferRecord> record) {
        if (!record || record->the_file_id_.empty()) {
            return;
        }
        auto db = context_->GetDatabase();
        if (!db->EnqueueAuditOutbox("file:" + record->the_file_id_ + ":end",
                                    FileTransferRecord::kUrlUpdateFileTransferRecord,
                                    record->AsUpdateJson(), TimeUtil::GetCurrentTimestamp())) {
            LOGE("Queue update file transfer audit failed: {}", record->the_file_id_);
        }
    }

    void WsPanelServer::FlushAuditOutbox() {
        auto db = context_->GetDatabase();
        const auto now = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
        auto pending = db->GetDueAuditOutbox(now);
        if (!pending.has_value()) {
            audit_flush_in_progress_ = false;
            return;
        }

        auto weak_self = weak_from_this();
        context_->PostNetworkTask([weak_self, item = std::move(pending.value())]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            auto settings = PxSettings::Instance();
            std::string serv_host = settings->GetConsoleServerHost();
            auto client = PxSettings::MakeConsoleHttpClient(
                serv_host, settings->GetConsoleServerPort(), item.endpoint_, 2000);
            client->SetHeader("x-px-appkey", grApp->GetAppkey());
            client->SetHeader("x-px-device-id", settings->GetDeviceId());
            auto resp = client->Post({}, item.payload_, "application/json");
            bool response_ok = false;
            if (resp.status == 200 && !resp.body.empty()) {
                try {
                    const auto body = nlohmann::json::parse(resp.body);
                    response_ok = body.value("code", -1) == 200;
                } catch (const std::exception&) {
                    response_ok = false;
                }
            }
            const auto completed_at = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());

            self->context_->PostDBTask([weak_self, item, resp_status = resp.status,
                                        response_ok,
                                        completed_at]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto db = self->context_->GetDatabase();
                if (response_ok) {
                    db->CompleteAuditOutbox(item.id_);
                } else {
                    const int attempts = item.attempts_ + 1;
                    const int shift = std::min(attempts, 8);
                    const int64_t delay_ms = std::min<int64_t>(300000, (1LL << shift) * 1000);
                    db->RetryAuditOutbox(item.id_, attempts, completed_at + delay_ms,
                                         std::format("http_status={}", resp_status));
                    LOGE("Audit outbox delivery failed, key: {}, status: {}, retry in {} ms",
                         item.event_key_, resp_status, delay_ms);
                }
                self->audit_flush_in_progress_ = false;
            });
        });
    }

    void WsPanelServer::PanelSocketOpened(const std::string& instance_id) {
        if (instance_id.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(panel_audit_mtx_);
        panel_instance_connections_[instance_id]++;
    }

    void WsPanelServer::PanelSocketClosed(const std::shared_ptr<WSSession>& session) {
        if (!session || session->stream_id_.empty()) {
            return;
        }
        const auto instance_id = session->stream_id_;
        const auto disconnected_at = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
        {
            std::lock_guard<std::mutex> lock(panel_audit_mtx_);
            auto it = panel_instance_connections_.find(instance_id);
            if (it != panel_instance_connections_.end() && it->second > 0) {
                it->second--;
            }
        }
        auto weak_self = weak_from_this();
        context_->PostDelayTask([weak_self, instance_id, disconnected_at]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->ClosePanelAuditRecordsIfOffline(instance_id, disconnected_at);
            }
        }, 5000);
    }

    void WsPanelServer::TrackPanelTransfer(uint64_t socket_fd, const std::string& file_id, bool connected) {
        if (file_id.empty()) {
            return;
        }
        auto session = panel_sessions_.TryGet(socket_fd);
        if (!session.has_value() || !session.value() || session.value()->stream_id_.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(panel_audit_mtx_);
        auto& ids = panel_transfer_ids_[session.value()->stream_id_];
        if (connected) {
            ids.insert(file_id);
        } else {
            ids.erase(file_id);
        }
    }

    void WsPanelServer::ClosePanelAuditRecordsIfOffline(const std::string& instance_id, int64_t disconnected_at) {
        std::unordered_set<std::string> transfer_ids;
        {
            std::lock_guard<std::mutex> lock(panel_audit_mtx_);
            if (panel_instance_connections_[instance_id] > 0) {
                return;
            }
            panel_instance_connections_.erase(instance_id);
            if (auto it = panel_transfer_ids_.find(instance_id); it != panel_transfer_ids_.end()) {
                transfer_ids = std::move(it->second);
                panel_transfer_ids_.erase(it);
            }
        }
        if (transfer_ids.empty()) {
            return;
        }

        auto weak_self = weak_from_this();
        context_->PostDBTask([weak_self, transfer_ids = std::move(transfer_ids), disconnected_at]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            for (const auto& file_id : transfer_ids) {
                auto record = self->ft_record_op_->GetFileTransferRecordByFileId(file_id);
                if (!record.has_value() || record.value()->end_ > 0) {
                    continue;
                }
                self->ft_record_op_->UpdateFileTransferRecord(
                    file_id, disconnected_at, false, "aborted", "client_disconnected", false);
                if (auto updated = self->ft_record_op_->GetFileTransferRecordByFileId(file_id); updated.has_value()) {
                    self->NotifyInsertFileTransferRecordToConsole(updated.value());
                    self->NotifyUpdateFileTransferRecordToConsole(updated.value());
                }
            }
        });
    }

    void WsPanelServer::RendererSocketOpened(const std::string& instance_id) {
        if (instance_id.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(renderer_audit_mtx_);
        renderer_instance_connections_[instance_id]++;
    }

    void WsPanelServer::RendererSocketClosed(const std::shared_ptr<WSSession>& session) {
        if (!session || session->stream_id_.empty()) {
            return;
        }
        const auto instance_id = session->stream_id_;
        const auto disconnected_at = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
        {
            std::lock_guard<std::mutex> lock(renderer_audit_mtx_);
            auto it = renderer_instance_connections_.find(instance_id);
            if (it != renderer_instance_connections_.end() && it->second > 0) {
                it->second--;
            }
        }
        auto weak_self = weak_from_this();
        context_->PostDelayTask([weak_self, instance_id, disconnected_at]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->CloseRendererAuditRecordsIfOffline(instance_id, disconnected_at);
            }
        }, 5000);
    }

    void WsPanelServer::TrackRendererVisit(uint64_t socket_fd, const std::string& conn_id, bool connected) {
        if (conn_id.empty()) {
            return;
        }
        auto session = renderer_sessions_.TryGet(socket_fd);
        if (!session.has_value() || !session.value() || session.value()->stream_id_.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(renderer_audit_mtx_);
        auto& ids = renderer_visit_ids_[session.value()->stream_id_];
        if (connected) {
            ids.insert(conn_id);
        } else {
            ids.erase(conn_id);
        }
    }

    void WsPanelServer::TrackRendererTransfer(uint64_t socket_fd, const std::string& file_id, bool connected) {
        if (file_id.empty()) {
            return;
        }
        auto session = renderer_sessions_.TryGet(socket_fd);
        if (!session.has_value() || !session.value() || session.value()->stream_id_.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(renderer_audit_mtx_);
        auto& ids = renderer_transfer_ids_[session.value()->stream_id_];
        if (connected) {
            ids.insert(file_id);
        } else {
            ids.erase(file_id);
        }
    }

    void WsPanelServer::CloseRendererAuditRecordsIfOffline(const std::string& instance_id, int64_t disconnected_at) {
        std::unordered_set<std::string> visit_ids;
        std::unordered_set<std::string> transfer_ids;
        {
            std::lock_guard<std::mutex> lock(renderer_audit_mtx_);
            if (renderer_instance_connections_[instance_id] > 0) {
                return;
            }
            renderer_instance_connections_.erase(instance_id);
            if (auto it = renderer_visit_ids_.find(instance_id); it != renderer_visit_ids_.end()) {
                visit_ids = std::move(it->second);
                renderer_visit_ids_.erase(it);
            }
            if (auto it = renderer_transfer_ids_.find(instance_id); it != renderer_transfer_ids_.end()) {
                transfer_ids = std::move(it->second);
                renderer_transfer_ids_.erase(it);
            }
        }
        if (visit_ids.empty() && transfer_ids.empty()) {
            return;
        }

        auto weak_self = weak_from_this();
        context_->PostDBTask([weak_self, visit_ids = std::move(visit_ids),
                              transfer_ids = std::move(transfer_ids), disconnected_at]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            for (const auto& conn_id : visit_ids) {
                auto record = self->visit_record_op_->GetVisitRecordConnId(conn_id);
                if (!record.has_value() || record.value()->end_ > 0) {
                    continue;
                }
                const auto duration = std::max<int64_t>(0, disconnected_at - record.value()->begin_);
                self->visit_record_op_->UpdateVisitRecord(
                    conn_id, disconnected_at, duration, "aborted", "renderer_disconnected", false);
                if (auto updated = self->visit_record_op_->GetVisitRecordConnId(conn_id); updated.has_value()) {
                    // Re-queue the begin as well: if the renderer died before
                    // the original begin reached Console, the terminal event can
                    // still be applied in order after retry.
                    self->NotifyInsertVisitRecordToConsole(updated.value());
                    self->NotifyUpdateVisitRecordToConsole(updated.value());
                }
            }
            for (const auto& file_id : transfer_ids) {
                auto record = self->ft_record_op_->GetFileTransferRecordByFileId(file_id);
                if (!record.has_value() || record.value()->end_ > 0) {
                    continue;
                }
                self->ft_record_op_->UpdateFileTransferRecord(
                    file_id, disconnected_at, false, "aborted", "renderer_disconnected", false);
                if (auto updated = self->ft_record_op_->GetFileTransferRecordByFileId(file_id); updated.has_value()) {
                    self->NotifyInsertFileTransferRecordToConsole(updated.value());
                    self->NotifyUpdateFileTransferRecordToConsole(updated.value());
                }
            }
        });
    }

    void WsPanelServer::NotifyEventIfNeeded(const std::shared_ptr<SysInfo>& sys_info) {
        if (!sys_info) {
            return;
        }
        auto event_mgr = context_->GetEventManager();
        if (!event_mgr) {
            LOGE("No event manager!");
            return;
        }

        // CPU
        if (sys_info->cpu_.usage_ > 80) {
            event_mgr->AddCpuEvent(sys_info->cpu_.usage_);
        }

        // Memory
        if (sys_info->mem_.total_gb_ > 0) {
            auto mem_usage = sys_info->mem_.used_gb_ * 100.0f / sys_info->mem_.total_gb_;
            if (mem_usage > 80) {
                event_mgr->AddMemoryEvent(mem_usage);
            }
        }

        // Disks
        for (const auto& disk : sys_info->disks_) {
            const auto path = disk.mount_on_;
            if (disk.total_gb_ > 0) {
                auto usage = (disk.total_gb_ - disk.available_gb_) * 100 / disk.total_gb_;
                if (usage > 90) {
                    event_mgr->AddDiskEvent(usage, path);
                }
            }
        }

        // GPU
        for (const auto& gpu : sys_info->gpus_) {
            if (gpu.gpu_utilization_ > 80) {
                event_mgr->AddGpuEvent(gpu.gpu_utilization_, gpu.id_, gpu.brand_);
            }
        }

    }

}
