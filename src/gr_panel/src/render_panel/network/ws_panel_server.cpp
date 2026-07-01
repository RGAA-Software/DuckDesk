//
// Created by RGAA on 2024-03-30.
//

#include "ws_panel_server.h"
#include <QApplication>
#include "apis.h"
#include "http_handler.h"
#include "tc_message.pb.h"
#include "render_panel/gr_settings.h"
#include <nlohmann/json.hpp>
#include "tc_common_new/http_client.h"
#include "tc_common_new/log.h"
#include "tc_common_new/time_util.h"
#include "tc_common_new/data.h"
#include "tc_common_new/file.h"
#include "tc_render_panel_message.pb.h"
#include "tc_client_panel_message.pb.h"
#include "render_panel/gr_context.h"
#include "render_panel/gr_app_messages.h"
#include "render_panel/gr_application.h"
#include "render_panel/gr_render_msg_processor.h"
#include "render_panel/transfer/file_transfer.h"
#include "render_panel/database/gr_database.h"
#include "render_panel/database/visit_record.h"
#include "render_panel/database/visit_record_operator.h"
#include "render_panel/database/file_transfer_record.h"
#include "render_panel/database/file_transfer_record_operator.h"
#include "tc_message_new/rp_proto_converter.h"
#include "tc_common_new/url_helper.h"
#include "tc_common_new/message_notifier.h"
#include "tc_qt_widget/translator/tc_translator.h"
#include "render_panel/companion/panel_companion.h"
#include "render_panel/gr_statistics.h"
#include "render_panel/devices/gr_device_manager.h"
#include "skin/interface/skin_interface.h"
#include "render_panel/spvr/gr_event_manager.h"

namespace tc
{

    static std::string kUrlPanel = "/panel";
    static std::string kUrlPanelRenderer = "/panel/renderer";
    static std::string kUrlFileTransfer = "/file/transfer";
    static std::string kUrlSysInfo = "/sys/info";

    // report visit info to cms
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

    std::shared_ptr<WsPanelServer> WsPanelServer::Make(const std::shared_ptr<GrApplication>& app) {
        return std::make_shared<WsPanelServer>(app);
    }

    WsPanelServer::WsPanelServer(const std::shared_ptr<GrApplication>& app) {
        app_ = app;
        stat_ = GrStatistics::Instance();
        context_ = app_->GetContext();
        http_handler_ = std::make_shared<HttpHandler>(app_);
        settings_ = GrSettings::Instance();
        visit_record_op_ = context_->GetDatabase()->GetVisitRecordOp();
        ft_record_op_ = context_->GetDatabase()->GetFileTransferRecordOp();

    }

    void WsPanelServer::Start() {
        exiting_ = false;
        auto weak_self = weak_from_this();
        msg_listener_ = context_->ObtainMessageListener();
        msg_listener_->Listen<MsgSecurityPasswordUpdated>([weak_self](const MsgSecurityPasswordUpdated& msg) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->RpSyncPanelInfo();
                    }
                });
            }
        });

        msg_listener_->Listen<MsgGrTimer1S>([weak_self](const MsgGrTimer1S& msg) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->RpSyncPanelInfo();
                    }
                });
            }
        });

        msg_listener_->Listen<MsgGrTimer5S>([weak_self](const MsgGrTimer5S& msg) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_ && self->panel_sessions_.Size() > 0) {
                        self->app_->GetDeviceManager()->UpdateUsedTime(5000);
                    }
                });
            }
        });

        msg_listener_->Listen<MsgHWInfo>([weak_self](const MsgHWInfo& msg) {
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
            if (self->panel_sessions_.Remove(socket_fd).has_value()) {
                LOGI("Panel;client disconnected: {}", socket_fd);
                LOGI("Panel;App server media close, media router size: {}", self->panel_sessions_.Size());
            }
            if (self->renderer_sessions_.Remove(socket_fd).has_value()) {
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
        //auto pwd = tc::File::OpenForRead(pwd_file)->ReadAllAsString();
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
        AddHttpGetRouter(kPathPing, [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandlePing(req, rep);
        });

        // response the information that equals to the QR Code
        AddHttpGetRouter(kPathSimpleInfo, [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleSimpleInfo(req, rep);
        });

        // response all apps that we found in system and added by user
        AddHttpGetRouter(kPathGames, [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleGames(req, rep);
        });

        // start game
        AddHttpPostRouter(kPathGameStart, [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleGameStart(req, rep);
        });

        // stop game
        AddHttpPostRouter(kPathGameStop, [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleGameStop(req, rep);
        });

        // running games
        AddHttpGetRouter(kPathRunningGames, [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleRunningGames(req, rep);
        });

        // stop the GammaRayRender.exe
        AddHttpGetRouter(kPathStopServer, [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleStopServer(req, rep);
        });

        // all running processes in th PC, equals the process list in TaskManager
        AddHttpGetRouter(kPathAllRunningProcesses, [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleAllRunningProcesses(req, rep);
        });

        // kill a process by pid
        AddHttpPostRouter(kPathKillProcess, [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleKillProcess(req, rep);
        });

        // res
        AddHttpGetRouter("/res/*", [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleResourcesFile(req, rep);
        });

        // cache
        AddHttpGetRouter("/steam/cache/*", [=, this](const auto& path, auto& req, auto& rep) {
            http_handler_->HandleSteamCacheFile(req, rep);
        });

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
        // file transfer
        AddWebsocketRouter(kUrlFileTransfer);
        // sys info
        AddWebsocketRouter(kUrlSysInfo);

        bool ret = server_->start("0.0.0.0", settings_->GetPanelServerPort());
        LOGI("App server start result: {}, port: {}", ret, settings_->GetPanelServerPort());

        context_->PostTask([weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->ScanAndFixUnclosedRecords();
            }
        });
    }

    void WsPanelServer::Exit() {
        exiting_ = true;
        msg_listener_ = nullptr;
        if (server_) {
            server_->stop_all_timers();
            server_->stop();
            server_.reset();
        }
        panel_sessions_.Clear();
        renderer_sessions_.Clear();
        ft_sessions_.Clear();
        sys_info_sess_.reset();
    }

    WsPanelServer::~WsPanelServer() {
        exiting_ = true;
        msg_listener_ = nullptr;
        if (server_) {
            server_->stop_all_timers();
            server_->stop();
        }
        panel_sessions_.Clear();
        renderer_sessions_.Clear();
        ft_sessions_.Clear();
        sys_info_sess_.reset();
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
                auto socket_fd = fn_get_socket_fd(sess_ptr);
                if (path == kUrlPanel) {
                    self->ParsePanelMessage(socket_fd, data);
                }
                else if (path == kUrlPanelRenderer) {
                    self->ParseRendererMessage(socket_fd, data);
                }
                else if (path == kUrlFileTransfer) {
                    self->ParseFtBinaryMessage(socket_fd, data);
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
                    self->panel_sessions_.Insert(socket_fd, ws_sess);
                    LOGI("Panel;client connect : {}", socket_fd);
                }
                else if (path == kUrlPanelRenderer) {
                    auto ws_sess = std::make_shared<WSSession>();
                    ws_sess->socket_fd_ = socket_fd;
                    ws_sess->session_ = sess_ptr;
                    self->renderer_sessions_.Insert(socket_fd, ws_sess);
                    LOGI("Renderer;client connect : {}", socket_fd);

                    sess_ptr->post_queued_event([weak_self]() {
                        if (auto self = weak_self.lock(); self && !self->exiting_) {
                            self->RpSyncPanelInfo();
                        }
                    });
                }
                else if (path == kUrlFileTransfer) {
                    auto ft_sess = std::make_shared<FtSession>();
                    ft_sess->socket_fd_ = socket_fd;
                    ft_sess->session_ = sess_ptr;
                    ft_sess->ch_ = std::make_shared<FileTransferChannel>(self->context_, sess_ptr);
                    self->ft_sessions_.Insert(socket_fd, ft_sess);
                    ft_sess->ch_->OnConnected();
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
                    self->panel_sessions_.Remove(socket_fd);
                }
                else if (path == kUrlPanelRenderer) {
                    self->renderer_sessions_.Remove(socket_fd);
                }
                else if (path == kUrlFileTransfer) {
                    if (auto ft_session = self->ft_sessions_.Remove(socket_fd); ft_session.has_value() && ft_session.value()) {
                        ft_session.value()->ch_->OnDisConnected();
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
        server_->bind<http::verb::get>(path, [=, this](http::web_request &req, http::web_response &rep) {
            cbk(path, req, rep);
        }, aop_log{});
    }

    void WsPanelServer::AddHttpPostRouter(const std::string& path,
        std::function<void(const std::string& path, http::web_request &req, http::web_response &rep)>&& cbk) {
        server_->bind<http::verb::post>(path, [=, this](http::web_request &req, http::web_response &rep) {
            cbk(path, req, rep);
        }, aop_log{});
    }

    void WsPanelServer::PostPanelMessage(const std::string& msg, bool only_inner) {
        panel_sessions_.VisitAll([=, this](uint64_t fd, std::shared_ptr<WSSession>& sess) {
            if (only_inner && sess->session_type_ != tccp::CpSessionType::kInnerServer) {
                return;
            }
            if (sess->session_) {
                sess->session_->async_send(msg);
            }
        });
    }

    bool WsPanelServer::ParsePanelMessage(uint64_t socket_fd, std::string_view msg) {
        auto proto_msg = std::make_shared<tccp::CpMessage>();
        if (!proto_msg->ParseFromArray(msg.data(), msg.size())) {
            return false;
        }
        if (proto_msg->type() == tccp::CpMessageType::kCpHello) {
            auto hello = proto_msg->hello();
            panel_sessions_.VisitAll([=](uint64_t k, std::shared_ptr<WSSession>& v) {
                if (v->socket_fd_ == socket_fd) {
                    v->session_type_ = hello.type();
                    LOGI("Update session type: {} for socket: {}", v->session_type_, socket_fd);
                }
            });

            context_->SendAppMessage(MsgClientConnectedPanel {
                .stream_id_ = proto_msg->stream_id(),
                .sess_type_ = hello.type(),
            });
        }
        else if (proto_msg->type() == tccp::CpMessageType::kCpHeartBeat) {
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
        else if (proto_msg->type() == tccp::kCpFileTransferBegin) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg]() {
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

                auto record = std::make_shared<FileTransferRecord>(FileTransferRecord{
                    .the_file_id_ = sub.the_file_id(),
                    .begin_ = sub.begin_timestamp(),
                    .end_ = 0,
                    .visitor_device_ = self->settings_->GetDeviceId().empty() ? ip_address : self->settings_->GetDeviceId(),
                    .target_device_ = sub.remote_device_id(),
                    .direction_ = sub.direction(),
                    .file_detail_ = sub.file_detail(),
                });

                self->ft_record_op_->InsertFileTransferRecord(record);

                self->NotifyInsertFileTransferRecordToCms(record);
            });
        }
        else if (proto_msg->type() == tccp::kCpFileTransferEnd) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto sub = proto_msg->ft_transfer_end();
                self->ft_record_op_->UpdateFileTransferRecord(sub.the_file_id(), sub.end_timestamp(), sub.success());

                if (const auto opt = self->ft_record_op_->GetFileTransferRecordByFileId(sub.the_file_id()); opt.has_value()) {
                    self->NotifyUpdateFileTransferRecordToCms(opt.value());
                }
            });
        }
        return true;
    }

    void WsPanelServer::RpSyncPanelInfo() {
        tcrp::RpMessage m;
        m.set_type(tcrp::RpMessageType::kSyncPanelInfo);
        auto sub = m.mutable_sync_panel_info();
        sub->set_device_id(settings_->GetDeviceId());
        sub->set_device_random_pwd(settings_->GetDeviceRandomPwd());
        sub->set_device_safety_pwd(settings_->GetDeviceSecurityPwd());
        sub->set_relay_host(settings_->GetRelayServerHost());
        sub->set_relay_port(std::to_string(settings_->GetRelayServerPort()));
        sub->set_can_be_operated(settings_->IsBeingOperatedEnabled());
        sub->set_relay_enabled(settings_->IsRelayEnabled());
        sub->set_language((int)tcTrMgr()->GetSelectedLanguage());
        sub->set_file_transfer_enabled(settings_->IsFileTransferEnabled());
        sub->set_audio_enabled(settings_->IsCaptureAudioEnabled());
        sub->set_appkey(grApp->GetAppkey());
        sub->set_max_transmit_speed(this->max_transmit_speed_);
        sub->set_max_receive_speed(this->max_receive_speed_);
        if (auto pc = grApp->GetCompanion(); pc && pc->GetAuth()) {
            sub->set_role(static_cast<int>(pc->GetAuth()->role_));
        }
        else {
            sub->set_role(1);
        }
        PostRendererMessage(tc::RpProtoAsData(&m));
    }

    void WsPanelServer::ParseFtBinaryMessage(uint64_t socket_fd, std::string_view msg) {
        if (auto sess = ft_sessions_.TryGet(socket_fd); sess.has_value() && sess.value()) {
            sess.value()->ch_->ParseBinaryMessage(msg);
        }
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
        auto proto_msg = std::make_shared<tcrp::RpMessage>();
        if (!proto_msg->ParseFromArray(msg.data(), msg.size())) {
            LOGE("Parse binary message failed.");
            return;
        }
        if (proto_msg->type() == tcrp::kRpCaptureStatistics) {
            auto statistics = std::make_shared<tcrp::RpCaptureStatistics>();
            statistics->CopyFrom(proto_msg->capture_statistics());
            context_->SendAppMessage(MsgCaptureStatistics{
                .msg_ = proto_msg,
                .statistics_ = statistics,
            });
        }
        else if (proto_msg->type() == tcrp::kRpServerAudioSpectrum) {
            //auto spectrum = proto_msg->renderer_audio_spectrum();
            auto spectrum = std::make_shared<tcrp::RpServerAudioSpectrum>();
            spectrum->CopyFrom(proto_msg->renderer_audio_spectrum());
            context_->SendAppMessage(MsgServerAudioSpectrum {
                .msg_ = proto_msg,
                .spectrum_ = spectrum,
            });
        }
        else if (proto_msg->type() == tcrp::kRpRestartServer) {
            context_->SendAppMessage(AppMsgRestartServer {});
        }
        else if (proto_msg->type() == tcrp::kRpPluginsInfo) {
            auto plugins_info = std::make_shared<tcrp::RpPluginsInfo>();
            plugins_info->CopyFrom(proto_msg->plugins_info());
            context_->SendAppMessage(MsgPluginsInfo {
                .plugins_info_ = plugins_info,
            });
        }
        else if (proto_msg->type() == tcrp::kRpClientConnected) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto ips = self->context_->GetIps();
                std::string ip_address;
                if (!ips.empty()) {
                    ip_address = ips[0].ip_addr_;
                }
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
                    .target_device_ = self->settings_->GetDeviceId().empty() ? ip_address : self->settings_->GetDeviceId(),
                });
                self->visit_record_op_->InsertVisitRecord(record);
                // notify cms
                self->NotifyInsertVisitRecordToCms(record);
            });
        }
        else if (proto_msg->type() == tcrp::kRpClientDisConnected) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto sub = proto_msg->client_disconnected();
                self->visit_record_op_->UpdateVisitRecord(sub.conn_id(), sub.end_timestamp(), sub.duration());

                auto record = std::make_shared<VisitRecord>(VisitRecord{
                    .conn_id_ = sub.conn_id(),
                    .end_ = sub.end_timestamp(),
                    .duration_ = sub.duration(),
                });
                self->NotifyUpdateVisitRecordToCms(record);
            });
            context_->SendAppMessage(MsgOneClientDisconnect{});
        }
        else if (proto_msg->type() == tcrp::kRpFileTransferBegin) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg]() {
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

                auto record = std::make_shared<FileTransferRecord>(FileTransferRecord {
                    .the_file_id_ = sub.the_file_id(),
                    .begin_ = sub.begin_timestamp(),
                    .end_ = 0,
                    .visitor_device_ = sub.visitor_device_id(),
                    .target_device_ = self->settings_->GetDeviceId().empty() ? ip_address : self->settings_->GetDeviceId(),
                    .direction_ = sub.direction(),
                    .file_detail_ = sub.file_detail(),
                });
                self->ft_record_op_->InsertFileTransferRecord(record);
                self->NotifyInsertFileTransferRecordToCms(record);
            });
        }
        else if (proto_msg->type() == tcrp::kRpFileTransferEnd) {
            auto weak_self = weak_from_this();
            context_->PostDBTask([weak_self, proto_msg]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto sub = proto_msg->ft_end();
                self->ft_record_op_->UpdateFileTransferRecord(sub.the_file_id(), sub.end_timestamp(), sub.success());

                if (const auto opt = self->ft_record_op_->GetFileTransferRecordByFileId(sub.the_file_id()); opt.has_value()) {
                    self->NotifyUpdateFileTransferRecordToCms(opt.value());
                }
            });
        }
        else if (proto_msg->type() == tcrp::kRpRawRenderMessage) {
            auto sub = proto_msg->raw_render_msg();
            auto rd_proto_msg = std::make_shared<tc::Message>();
            if (!rd_proto_msg->ParseFromString(sub.msg())) {
                LOGE("kRpRawRenderMessage parse failed");
                return;
            }
            auto processor = app_->GetRenderMsgProcessor();
            processor->OnMessage(rd_proto_msg);
        }
        else if (proto_msg->type() == tcrp::kRpRelayAlive) {
            auto sub = proto_msg->relay_alive();
            stat_->UpdateRelayAlive(sub.device_id(), sub.timestamp());
        }
        else if (proto_msg->type() == tcrp::kRpMonitorChanged) {
            context_->SendAppMessage(MsgMonitorChanged{});
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
            tcrp::RpMessage rp_msg;
            rp_msg.set_type(tcrp::RpMessageType::kRpHardwareInfo);
            auto sub = rp_msg.mutable_hw_info();
            sub->set_json_msg(sys_info->raw_json_msg_);
            sub->set_current_cpu_freq(companion->GetCurrentCpuFrequency());
            PostRendererMessage(tc::RpProtoAsData(&rp_msg));
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
        const auto cutoff = now - 60 * 1000; // only records older than 60s

        auto visits = db->ScanUnclosedVisitRecords(cutoff);
        for (auto& r : visits) {
            r->end_ = now;
            r->duration_ = std::max<int64_t>(0, now - r->begin_);
            visit_record_op_->InsertVisitRecord(r);
            NotifyUpdateVisitRecordToCms(r);
        }

        auto transfers = db->ScanUnclosedFileTransferRecords(cutoff);
        for (auto& r : transfers) {
            r->end_ = now;
            r->success_ = false;
            r->duration_ = std::max<int64_t>(0, now - r->begin_);
            ft_record_op_->InsertFileTransferRecord(r);
            NotifyUpdateFileTransferRecordToCms(r);
        }

        LOGI("ScanAndFixUnclosedRecords: fixed {} visit(s), {} file transfer(s)", visits.size(), transfers.size());
    }

    void WsPanelServer::NotifyInsertVisitRecordToCms(const std::shared_ptr<VisitRecord> record) {
        if (!record) {
            return;
        }
        auto settings = GrSettings::Instance();
        std::string serv_host = settings->GetSpvrServerHost();
        auto client = HttpClient::MakeSSL(serv_host, settings->GetSpvrServerPort(), kUrlVisitRecord, 2000);
        auto appkey = grApp->GetAppkey();
        auto resp = client->Post({
            {"appkey", appkey}
            }, record->AsJson2(), "application/json");

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("NotifyInsertVisitRecordToCms failed: {}", resp.status);
        }
    }

    void WsPanelServer::NotifyUpdateVisitRecordToCms(const std::shared_ptr<VisitRecord> record) {
        if (!record) {
            return;
        }
        auto settings = GrSettings::Instance();
        std::string serv_host = settings->GetSpvrServerHost();
        auto client = HttpClient::MakeSSL(serv_host, settings->GetSpvrServerPort(), kUrlUpdateVisitRecord, 2000);
        auto appkey = grApp->GetAppkey();
        auto resp = client->Post({
            {"appkey", appkey}
            }, record->AsUpdateJson(), "application/json");

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("NotifyUpdateVisitRecordToCms failed: {}", resp.status);
        }
    }

    void WsPanelServer::NotifyInsertFileTransferRecordToCms(const std::shared_ptr<FileTransferRecord> record) {
        if (!record) {
            return;
        }
        auto settings = GrSettings::Instance();
        std::string serv_host = settings->GetSpvrServerHost();
        auto client = HttpClient::MakeSSL(serv_host, settings->GetSpvrServerPort(), FileTransferRecord::kUrlInsertFileTransferRecord, 2000);
        auto appkey = grApp->GetAppkey();
        auto resp = client->Post({
            {"appkey", appkey}
            }, record->AsJson2(), "application/json");

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("NotifyInsertFileTransferRecordToCms failed: {}", resp.status);
        }
    }

    void WsPanelServer::NotifyUpdateFileTransferRecordToCms(const std::shared_ptr<FileTransferRecord> record) {
        if (!record) {
            return;
        }
        auto settings = GrSettings::Instance();
        std::string serv_host = settings->GetSpvrServerHost();
        auto client = HttpClient::MakeSSL(serv_host, settings->GetSpvrServerPort(), FileTransferRecord::kUrlUpdateFileTransferRecord, 2000);
        auto appkey = grApp->GetAppkey();
        auto resp = client->Post({
            {"appkey", appkey}
            }, record->AsUpdateJson(), "application/json");

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("NotifyUpdateFileTransferRecordToCms failed: {}", resp.status);
        }
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
