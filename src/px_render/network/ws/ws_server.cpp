//
// Created by RGAA on 2024/3/1.
//

#include "ws_server.h"

#include <array>
#include <atomic>
#include <algorithm>
#include <bit>
#include <memory>
#include <optional>
#include <filesystem>
#include "px_common_new/log.h"
#include "px_common_new/privacy_log.h"
#include "px_common_new/time_util.h"
#include "px_common_new/data.h"
#include "px_common_new/file.h"
#include "px_common_new/folder_util.h"
#include "network/ws_media_router.h"
#include "ws_stream_router.h"
#include "ws_filetransfer_router.h"
#include "ws_user_proxy_router.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/modules/module_ids.h"
#include "px_capture_new/capture_message.h"
#include "ws_plugin.h"
#include "px_common_new/url_helper.h"
#include "px_common_new/ws_control_signal.h"
#include "http_handler.h"
#include "ws_callback_workflow.h"
#include "px_common_new/async_operation.h"
#include "px_common_new/async_result.h"
#include "px_render/architecture/runtime/await_callback.h"

static std::string kUrlMedia = "/media";
static std::string kUrlFileTransfer = "/file/transfer";
static std::string kUrlUserProxy = "/user-proxy";
static std::string kUrlIpc = "/ipc";
static std::string kApiPing = "/api/ping";
static std::string kApiVerifySecurityPassword = "/verify/security/password";
static std::string kApiGetRenderConfiguration = "/get/render/configuration";
static std::string kApiPanelStreamMessage = "/panel/stream/message";
static std::string kApiAllocLocalRtc = "/alloc/local/rtc";
static std::string kUrlWebClient = "/web_client";
static std::string kUrlWebClientWildcard = "/web_client/*";

// /ipc carries raw captured frames up and user keyboard/mouse events down.
// It must only ever talk to the injected dll on the same machine.
static bool IsLoopbackAddress(const std::string& addr) {
    return addr == "127.0.0.1" || addr == "::1" || addr == "::ffff:127.0.0.1";
}

template <typename WireValue>
static std::optional<WireValue> DecodeWireValue(
    const std::string_view bytes) {
    static_assert(std::is_trivially_copyable_v<WireValue>);
    if (bytes.size() < sizeof(WireValue)) {
        return std::nullopt;
    }
    std::array<char, sizeof(WireValue)> storage{};
    std::copy_n(bytes.begin(), storage.size(), storage.begin());
    return std::bit_cast<WireValue>(storage);
}

// /ipc pid 清扫用:进程是否仍存活(句柄可开且未退出)
static bool IsIpcProcessAlive(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        return false;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

namespace px
{
    struct WsTicketAdmission {
        std::vector<std::string> permissions_;
        std::string logical_session_id_;
        std::string stream_id_;
        std::string join_mode_;
        std::string subject_id_;
        int64_t expires_at_ms_ = 0;
        bool allow_observer_ = true;
        bool allow_takeover_ = true;
    };

    static void RejectWebSocketSession(
        std::shared_ptr<asio2::http_session> session,
        const std::string_view control_signal) {
        if (!session || control_signal.empty()) {
            return;
        }
        auto payload = std::make_shared<std::string>(control_signal);
        // This helper runs from websocket listener::open. asio2 finishes the
        // upgrade immediately after that callback returns, so queue the frame
        // instead of attempting to write before the websocket is ready.
        session->post_queued_event([session = std::move(session), payload]() {
            session->ws_stream().text(true);
            session->async_send(*payload, [session, payload](std::size_t) {
                static_cast<void>(payload);
                session->stop();
            });
        });
    }

    static void DispatchCloseLogicalSessionBinding(
        const std::weak_ptr<WsPlugin>& plugin,
        const std::string& logical_session_id,
        const std::string& binding_id) {
        const auto owner = plugin.lock();
        if (!owner || logical_session_id.empty() || binding_id.empty()) {
            return;
        }
        const auto event =
            std::make_shared<PxPluginCloseLogicalSessionBindingEvent>();
        event->logical_session_id_ = logical_session_id;
        event->binding_id_ = binding_id;
        owner->CallbackEvent(event);
    }

    static PxAwaitable<PxResult<WsTicketAdmission>> RedeemWsTicketAsync(
        const std::weak_ptr<WsPlugin>& plugin,
        const std::unordered_map<std::string, std::string>& params) {
        const auto ticket_it = params.find("ticket");
        if (ticket_it == params.end() || ticket_it->second.empty()) {
            co_return PxResult<WsTicketAdmission>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kInvalidArgument, "ws_ticket_redeem",
                "connection ticket is missing"));
        }
        const auto nonce_it = params.find("client_nonce");
        if (nonce_it == params.end() || nonce_it->second.empty()) {
            co_return PxResult<WsTicketAdmission>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kInvalidArgument, "ws_ticket_redeem",
                "client nonce is missing"));
        }
        const auto ticket = ticket_it->second;
        const auto nonce = nonce_it->second;
        std::string instance_id;
        if (const auto instance = params.find("instance_id"); instance != params.end()) {
            instance_id = instance->second;
        }
        co_return co_await render::AwaitOwnedCallback<WsTicketAdmission>(
            [plugin, ticket, nonce, instance_id](
                render::OwnedCallbackCompletion<WsTicketAdmission> completion) {
                const auto owner = plugin.lock();
                if (!owner) {
                    return false;
                }
                const auto event =
                    std::make_shared<PxPluginRedeemConnectionTicketEvent>();
                event->ticket_ = ticket;
                event->client_nonce_ = nonce;
                event->instance_id_ = instance_id;
                event->callback_ = [completion = std::move(completion)](
                    const bool ok, const std::string& code,
                    const std::vector<std::string>& permissions,
                    const std::string&, const std::string& logical_session_id,
                    const std::string& stream_id, const std::string& join_mode,
                    const std::string& subject_id, const int64_t expires_at_ms,
                    const bool allow_observer, const bool allow_takeover) {
                    if (!ok || stream_id.empty()) {
                        completion(PxResult<WsTicketAdmission>::Failure(
                            MakePxAsyncError(
                                PxAsyncErrorCode::kServiceRejected,
                                "ws_ticket_redeem",
                                code.empty() ? "ticket was rejected" : code,
                                false, "SESSION_TICKET_REJECTED")));
                        return;
                    }
                    completion(PxResult<WsTicketAdmission>::Success(
                        WsTicketAdmission{
                            .permissions_ = permissions,
                            .logical_session_id_ = logical_session_id,
                            .stream_id_ = stream_id,
                            .join_mode_ = join_mode,
                            .subject_id_ = subject_id,
                            .expires_at_ms_ = expires_at_ms,
                            .allow_observer_ = allow_observer,
                            .allow_takeover_ = allow_takeover,
                        }));
                };
                owner->CallbackEvent(event);
                return true;
            },
            std::chrono::steady_clock::now() + std::chrono::seconds(3),
            "ws_ticket_redeem");
    }

    static PxAwaitable<PxResult<LogicalSessionAdmission>> AdmitWsSessionAsync(
        const std::weak_ptr<WsPlugin>& plugin,
        LogicalSessionGrant grant,
        const LogicalSessionTransport transport,
        std::string binding_id) {
        const auto logical_session_id = grant.logical_session_id;
        co_return co_await AwaitWsValueCallback<LogicalSessionAdmission>(
            [plugin, grant = std::move(grant), transport, binding_id](
                std::function<void(LogicalSessionAdmission)> completion) {
                const auto owner = plugin.lock();
                if (!owner) {
                    return false;
                }
                const auto event =
                    std::make_shared<PxPluginAdmitLogicalSessionEvent>();
                event->grant_ = grant;
                event->transport_ = transport;
                event->binding_id_ = binding_id;
                event->callback_ = std::move(completion);
                owner->CallbackEvent(event);
                return true;
            },
            std::chrono::steady_clock::now() + std::chrono::seconds(3),
            "ws_session_admit",
            [plugin, logical_session_id, binding_id](
                const LogicalSessionAdmission& admission) {
                if (admission.code ==
                    LogicalSessionAdmissionCode::kAccepted) {
                    DispatchCloseLogicalSessionBinding(
                        plugin, logical_session_id, binding_id);
                }
            });
    }

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

    // wire 级扫描 px.Message 的 type 字段(field 10, varint, tag=0x50),
    // 识别 kVideoFrame(30)/kAudioFrame(40)——与 webrtc_local_library.cpp 的
    // IsMediaFrameMessage 同一做法(不引 protobuf 头,避免 absl 冲突)。
    // udp_media 客户端的音视频帧都走 UDP,ws 下发前用它过滤。
    static bool IsMediaFrameMessage(const std::shared_ptr<Data>& msg) {
        if (!msg || msg->Size() < 2) {
            return false;
        }
        const auto* p = (const uint8_t*)msg->DataAddr();
        const size_t n = (size_t)msg->Size();
        size_t i = 0;
        auto read_varint = [&](uint64_t& out) -> bool {
            out = 0;
            int shift = 0;
            while (i < n && shift < 64) {
                uint8_t b = p[i++];
                out |= (uint64_t)(b & 0x7F) << shift;
                if (!(b & 0x80)) {
                    return true;
                }
                shift += 7;
            }
            return false;
        };
        // 逐字段扫描,找到 field 10(type)为止;负载按 wire type 跳过
        while (i < n) {
            uint64_t tag = 0;
            if (!read_varint(tag)) {
                return false;
            }
            const uint32_t field = (uint32_t)(tag >> 3);
            const uint32_t wire = (uint32_t)(tag & 0x7);
            if (field == 10 && wire == 0) {
                uint64_t type = 0;
                if (!read_varint(type)) {
                    return false;
                }
                // px_message.proto: kVideoFrame = 30, kAudioFrame = 40
                // udp_media 客户端的音视频都走 UDP,ws 下发前都过滤掉
                return type == 30 || type == 40;
            }
            switch (wire) {
            case 0: { uint64_t v; if (!read_varint(v)) { return false; } break; }
            case 1: i += 8; break;
            case 2: {
                uint64_t len = 0;
                if (!read_varint(len)) { return false; }
                i += (size_t)len;
                break;
            }
            case 5: i += 4; break;
            default: return false; // group 等不支持,视为非媒体帧
            }
            if (i > n) {
                return false;
            }
        }
        return false;
    }

    static std::optional<int> ExtractProtocolMessageType(const std::shared_ptr<Data>& msg) {
        if (!msg) {
            return std::nullopt;
        }
        const auto payload = msg->AsString();
        size_t offset = 0;
        auto read_varint = [&payload, &offset]() -> std::optional<uint64_t> {
            uint64_t value = 0;
            for (int shift = 0; shift < 64 && offset < payload.size(); shift += 7) {
                const auto byte = static_cast<uint8_t>(payload[offset++]);
                value |= static_cast<uint64_t>(byte & 0x7f) << shift;
                if ((byte & 0x80) == 0) {
                    return value;
                }
            }
            return std::nullopt;
        };
        while (offset < payload.size()) {
            const auto tag = read_varint();
            if (!tag || *tag == 0) {
                return std::nullopt;
            }
            const auto field = static_cast<uint32_t>(*tag >> 3);
            const auto wire = static_cast<uint32_t>(*tag & 0x7);
            if (field == 10 && wire == 0) {
                const auto value = read_varint();
                return value ? std::optional<int>(static_cast<int>(*value)) : std::nullopt;
            }
            switch (wire) {
            case 0:
                if (!read_varint()) return std::nullopt;
                break;
            case 1:
                offset += 8;
                break;
            case 2: {
                const auto length = read_varint();
                if (!length || *length > payload.size() - offset) return std::nullopt;
                offset += static_cast<size_t>(*length);
                break;
            }
            case 5:
                offset += 4;
                break;
            default:
                return std::nullopt;
            }
            if (offset > payload.size()) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    static bool IsClipboardProtocolMessage(const std::shared_ptr<Data>& msg) {
        const auto type = ExtractProtocolMessageType(msg);
        return type && (*type == 160 || *type == 161 || *type == 349
                        || *type == 350 || *type == 351 || *type == 360);
    }

    WsPluginServer::WsPluginServer(std::weak_ptr<WsPlugin> plugin,
                                   const uint16_t listen_port)
        : plugin_(std::move(plugin)), listen_port_(listen_port) {}

    void WsPluginServer::Start() {
        if (server_ || async_runtime_) {
            Exit();
        }
        exiting_ = false;
        transport_performance_.Reset(std::chrono::steady_clock::now());
        async_runtime_ = PxAsyncRuntime::Create({.worker_threads = 2});
        if (!async_runtime_ || !async_runtime_->Start()) {
            LOGE("event=module.start component=net_ws code=ASYNC_RUNTIME_START_FAILED "
                 "operation=start_control_workflows outcome=failed recoverable=false");
            async_runtime_.reset();
            return;
        }
        async_scope_ = PxAsyncScope::Create(
            async_runtime_, PxAsyncLane::kControl);
        http_handler_ = std::make_shared<HttpHandler>(plugin_, async_scope_);
        auto weak_self = weak_from_this();
        server_ = std::make_shared<asio2::http_server>();
        server_->bind_disconnect([weak_self](std::shared_ptr<asio2::http_session>& sess_ptr) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            auto socket_fd = (uint64_t)sess_ptr->socket().native_handle();
            //LOGI("client disconnected: {}", socket_fd);
            if (auto opt_val = self->stream_routers_.Remove(socket_fd); opt_val.has_value()) {
                const auto& val = opt_val.value();
                self->UpdateUdpMediaAssociation(val->udp_media_association_code_,
                                                val->logical_session_id_, val->stream_id_, false, true);
                self->CloseLogicalSessionBinding(val->logical_session_id_, val->binding_id_);
                self->NotifyMediaClientDisConnected(val->conn_id_, val->stream_id_, val->visitor_device_id_, val->created_timestamp_, val->binding_id_, val->logical_session_id_);
                LOGI("event=session.close component=net_ws outcome=removed "
                     "device={}", PrivacyLogId(val->visitor_device_id_));
                LOGI("App server media close, media router size: {}", self->stream_routers_.Size());
            }
            else if (auto removed = self->ft_routers_.Remove(socket_fd);
                     removed.has_value()) {
                const auto& router = removed.value();
                self->CloseLogicalSessionBinding(router->logical_session_id_, router->binding_id_);
                router->OnClose(sess_ptr);
                self->NotifyMediaClientDisConnected(
                    router->conn_id_, router->stream_id_, router->device_id_,
                    router->created_timestamp_, router->binding_id_, router->logical_session_id_);
            }
            else if (self->ipc_sessions_.Remove(socket_fd).has_value()) {
                LOGI("IPC (/ipc) session removed on disconnect, remaining={}",
                     self->ipc_sessions_.Size());
            }
        });

        server_->support_websocket(true);
        ws_data_ = std::make_shared<WsData>(WsData{.plugin_ = plugin_});

        //auto exe_dir = qApp->applicationDirPath().toStdString();
        //auto pwd_file = std::format("{}/certs/password", exe_dir);
        //auto pwd = (File::OpenForRead(pwd_file))->ReadAllAsString();
        //server_->set_cert_file(
        //    "",
        //    std::format("{}/certs/server.crt", exe_dir),
        //    std::format("{}/certs/server.key", exe_dir),
        //    pwd);

        //if (asio2::get_last_error()) {
        //    LOGE("load cert files failed: {}", asio2::last_error_msg());
        //}
        //else {
        //    LOGE("set cert files success.");
        //}
        //server_->set_verify_mode(asio::ssl::verify_peer);

        // media websocket
        AddWebsocketRouter(kUrlMedia);
        AddWebsocketRouter(kUrlFileTransfer);
        // game-hook DLL (px_gh) posts CaptureVideoFrame here
        AddIpcRouter();
#if PX_USER_PROXY_ENABLED
        AddUserProxyRouter();
#endif

        // ping
        AddHttpRouter(kApiPing, [weak_self](const std::string&, std::shared_ptr<asio2::http_session>&, http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandlePing(req, rep);
            }
        });

        // verify security pwd
        AddHttpRouter(kApiVerifySecurityPassword, [weak_self](const std::string&, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandleVerifySecurityPassword(session_ptr, req, rep);
            }
        });

        // get render configuration
        AddHttpRouter(kApiGetRenderConfiguration, [weak_self](const std::string&, std::shared_ptr<asio2::http_session>&, http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandleGetRenderConfiguration(req, rep);
            }
        });

        //
        AddHttpRouter(kApiPanelStreamMessage, [weak_self](const std::string&, std::shared_ptr<asio2::http_session>&, http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandlePanelStreamMessage(req, rep);
            }
        });

        // kApiAllocLocalRtc
        AddHttpRouter(kApiAllocLocalRtc, [weak_self](const std::string&, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandleAllocLocalRtc(session_ptr, req, rep);
            }
        });

        // static web client pages (SPA), served from {exe_dir}/web_client
        AddWebClientRouter();

        if (listen_port_ <= 0) {
            LOGE("event=module.start component=net_ws code=WS_LISTEN_PORT_INVALID "
                 "operation=start_server outcome=failed recoverable=false port={}",
                 listen_port_);
        }
        bool ret = server_->start("0.0.0.0", std::to_string(listen_port_));
        LOGI("App server start result: {}, listen port: {}", ret, listen_port_);
    }

    void WsPluginServer::Exit() {
        exiting_ = true;
        if (async_scope_) {
            const auto called_from_scope = async_scope_->IsScopeThread();
            const auto drained = called_from_scope
                ? (async_scope_->BeginStop(), false)
                : async_scope_->StopAndWait(std::chrono::seconds(5));
            if (!drained && !called_from_scope) {
                LOGE("event=async.scope_drain component=net_ws "
                     "code=ASYNC_SCOPE_DRAIN_TIMEOUT operation=stop_control_workflows "
                     "outcome=timeout recoverable=false outstanding={}",
                     async_scope_->GetStatistics().outstanding);
            } else if (called_from_scope) {
                LOGI("event=async.scope_drain component=net_ws "
                     "operation=stop_control_workflows outcome=deferred "
                     "reason=shutdown_requested_from_callback outstanding={}",
                     async_scope_->GetStatistics().outstanding);
            }
        }
        if (server_) {
            server_->stop_all_timers();
            server_->stop();
        }
        async_scope_.reset();
        http_handler_.reset();
        if (async_runtime_) {
            async_runtime_->RequestDrain();
            async_runtime_->RequestStop();
            async_runtime_->Join();
            async_runtime_.reset();
        }
        server_.reset();
        ws_data_.reset();
        user_proxy_router_.reset();
        stream_routers_.Clear();
        ft_routers_.Clear();
        ipc_sessions_.Clear();
        ipc_session_pids_.Clear();
    }

    void WsPluginServer::PostNetMessage(std::shared_ptr<Data> msg) {
        if (!msg) {
            return;
        }
        const bool is_media_frame = IsMediaFrameMessage(msg);
        const bool is_clipboard_message = IsClipboardProtocolMessage(msg);
        stream_routers_.ApplyAll([=](const uint64_t& socket_fd, const std::shared_ptr<WsStreamRouter>& router) {
            static_cast<void>(socket_fd);
            if (is_clipboard_message && !router->clipboard_allowed_.load()) {
                transport_performance_.ObserveDropped();
                const auto decision = warning_log_gate_.Evaluate(
                    "clipboard:" + router->stream_id_,
                    std::chrono::steady_clock::now());
                if (decision.emit) {
                    LOGW("event=transport.send component=net_ws "
                         "code=SESSION_CAPABILITY_DENIED operation=clipboard "
                         "outcome=dropped recoverable=true stream={} suppressed={}",
                         PrivacyLogId(router->stream_id_),
                         decision.suppressed_since_last_emit);
                }
                return;
            }
            // udp_media 客户端的媒体帧走 UDP 通道,ws 只发控制消息
            if (is_media_frame && router->udp_media_.load()) {
                return;
            }
            router->PostBinaryMessage(msg);
            transport_performance_.ObserveOutbound(msg->Size());
        });
    }

    void WsPluginServer::UpdateLogicalSessionCapabilities(
        const PxLogicalSessionCapabilityUpdate& update) {
        const bool clipboard_allowed = std::find(
            update.permissions_.begin(), update.permissions_.end(), "clipboard")
            != update.permissions_.end();
        const bool file_allowed = std::find(
            update.permissions_.begin(), update.permissions_.end(), "file")
            != update.permissions_.end();
        stream_routers_.ApplyAll([&update, clipboard_allowed, file_allowed](
            const uint64_t&, const std::shared_ptr<WsStreamRouter>& router) {
            if (router && router->stream_id_ == update.stream_id_) {
                router->clipboard_allowed_.store(clipboard_allowed);
                router->file_allowed_.store(file_allowed);
            }
        });
        ft_routers_.ApplyAll([&update, file_allowed](
            const uint64_t&, const std::shared_ptr<WsFileTransferRouter>& router) {
            if (router && router->stream_id_ == update.stream_id_) {
                router->file_allowed_.store(file_allowed);
            }
        });
    }

    void WsPluginServer::PostIpcBinaryMessage(std::shared_ptr<Data> msg) {
        if (!msg || msg->Size() <= 0) {
            return;
        }
        const std::string payload = msg->AsString();
        int sent = 0;
        ipc_sessions_.ApplyAll([&](const uint64_t&, const std::shared_ptr<asio2::http_session>& sess) {
            if (!sess || !sess->is_started()) {
                return;
            }
            sess->async_send(payload);
            transport_performance_.ObserveOutbound(payload.size());
            ++sent;
        });
        if (sent == 0) {
            transport_performance_.ObserveDropped();
            static std::atomic<uint64_t> s_drop{0};
            const auto n = ++s_drop;
            if (n == 1 || (n % 100) == 0) {
                LOGW("event=transport.send component=net_ws "
                     "code=TRANSPORT_ROUTE_UNAVAILABLE operation=ipc_downlink "
                     "outcome=dropped recoverable=true count={} bytes={}",
                     n, payload.size());
            }
        } else {
            static std::atomic<uint64_t> s_ok{0};
            const auto n = ++s_ok;
            if (n <= 5 || (n % 200) == 0) {
                LOGI("PostIpcBinaryMessage: sent n={} sessions={} bytes={}", n, sent, payload.size());
            }
        }
    }

    bool WsPluginServer::PostTargetStreamMessage(const std::string& stream_id, std::shared_ptr<Data> msg) {
        bool found_target_stream = false;
        const bool is_media_frame = IsMediaFrameMessage(msg);
        const bool is_clipboard_message = IsClipboardProtocolMessage(msg);
        stream_routers_.ApplyAll([=, &found_target_stream](const uint64_t& socket_fd, const std::shared_ptr<WsStreamRouter>& router) {
            static_cast<void>(socket_fd);
            if (stream_id == router->stream_id_ || stream_id.empty()) {
                found_target_stream = true;
                if (is_clipboard_message && !router->clipboard_allowed_.load()) {
                    transport_performance_.ObserveDropped();
                    const auto decision = warning_log_gate_.Evaluate(
                        "target_clipboard:" + router->stream_id_,
                        std::chrono::steady_clock::now());
                    if (decision.emit) {
                        LOGW("event=transport.send component=net_ws "
                             "code=SESSION_CAPABILITY_DENIED operation=clipboard "
                             "outcome=dropped recoverable=true stream={} suppressed={}",
                             PrivacyLogId(router->stream_id_),
                             decision.suppressed_since_last_emit);
                    }
                    return;
                }
                // udp_media 客户端的媒体帧走 UDP 通道,ws 只发控制消息
                if (is_media_frame && router->udp_media_.load()) {
                    return;
                }
                router->PostBinaryMessage(msg);
                transport_performance_.ObserveOutbound(msg ? msg->Size() : 0);
            }
        });
        return found_target_stream;
    }

    FileTransferSendResult WsPluginServer::PostTargetFileTransferMessage(
        const std::string& stream_id,
        const std::shared_ptr<Data>& msg,
        const std::string& connection_instance_id) {
        if (!msg) {
            return FileTransferSendResult::TransportError(
                "WebSocket file-transfer payload is empty");
        }
        auto result = FileTransferSendResult::Disconnected(
            "WebSocket file-transfer route was not found");
        ft_routers_.ApplyAll([&](const uint64_t& socket_fd, const std::shared_ptr<WsFileTransferRouter>& router) {
            static_cast<void>(socket_fd);
            const bool matches = !connection_instance_id.empty()
                ? connection_instance_id == router->binding_id_
                : (stream_id == router->stream_id_ || stream_id.empty());
            if (matches) {
                result = router->TryPostBinaryMessage(msg);
            }
        });
        if (result.status() == FileTransferSendStatus::kAccepted
            || result.status() == FileTransferSendStatus::kBusy) {
            if (result.status() == FileTransferSendStatus::kAccepted) {
                transport_performance_.ObserveOutbound(msg->Size());
            }
            return result;
        }
        // UDP-direct intentionally multiplexes reliable file traffic over its
        // authenticated WS control binding. This avoids a second redemption of
        // the one-time Console ticket and keeps all non-media logic reliable.
        stream_routers_.ApplyAll([&](const uint64_t& socket_fd, const std::shared_ptr<WsStreamRouter>& router) {
            static_cast<void>(socket_fd);
            const bool matches = !connection_instance_id.empty()
                ? connection_instance_id == router->binding_id_
                : (stream_id == router->stream_id_ || stream_id.empty());
            if (matches) {
                result = router->TryPostFileTransferMessage(msg);
            }
        });
        if (result.status() == FileTransferSendStatus::kAccepted) {
            transport_performance_.ObserveOutbound(msg->Size());
        }
        return result;
    }

    int WsPluginServer::GetConnectedClientsCount() {
        return (int)stream_routers_.Size();
    }

    bool WsPluginServer::IsOnlyAudioClients() {
        bool only_audio_client = true;
        stream_routers_.VisitAllCond([&](auto k, auto& v) -> bool {
            if (v->enable_video_) {
                only_audio_client = false;
                return true;
            }
            return false;
        });
        return only_audio_client;
    }

    bool WsPluginServer::IsWorking() {
        return server_ && server_->is_started();
    }

    void WsPluginServer::PostUserProxyMessage(std::shared_ptr<Data> msg) {
#if PX_USER_PROXY_ENABLED
        if (!msg) {
            return;
        }
        if (user_proxy_router_ && user_proxy_router_->IsConnected()) {
            LOGI("PostUserProxyMessage ok, len={}", msg->Size());
            user_proxy_router_->PostBinaryMessage(msg);
            transport_performance_.ObserveOutbound(msg->Size());
        } else {
            LOGW("event=transport.send component=net_ws "
                 "code=TRANSPORT_ROUTE_UNAVAILABLE operation=user_proxy "
                 "outcome=dropped recoverable=true bytes={}", msg->Size());
            transport_performance_.ObserveDropped();
        }
#endif
    }

    bool WsPluginServer::IsUserProxyConnected() {
#if PX_USER_PROXY_ENABLED
        return user_proxy_router_ && user_proxy_router_->IsConnected();
#else
        return false;
#endif
    }

    void WsPluginServer::AddUserProxyRouter() {
        user_proxy_router_ = WsUserProxyRouter::Make(ws_data_);
        auto weak_self = weak_from_this();
        auto weak_router = std::weak_ptr<WsUserProxyRouter>(user_proxy_router_);
        auto fn_get_socket_fd = [](std::shared_ptr<asio2::http_session> &sess_ptr) -> uint64_t {
            return (uint64_t)sess_ptr->socket().native_handle();
        };
        server_->bind(kUrlUserProxy, websocket::listener<asio2::http_session>{}
            .on("message", [weak_self, weak_router, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr, std::string_view data) {
                if (const auto self = weak_self.lock(); self && !self->exiting_) {
                    self->transport_performance_.ObserveInbound(data.size());
                }
                if (auto router = weak_router.lock()) {
                    auto socket_fd = fn_get_socket_fd(sess_ptr);
                    router->OnMessage(sess_ptr, socket_fd, data);
                }
            })
            .on("open", [weak_self, weak_router, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                if (auto router = weak_router.lock()) {
                    router->OnOpen(sess_ptr);
                    self->transport_performance_.ObserveConnected();
                }
            })
            .on("close", [weak_self, weak_router, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                if (auto router = weak_router.lock()) {
                    router->OnClose(sess_ptr);
                }
                if (const auto self = weak_self.lock()) {
                    self->transport_performance_.ObserveDisconnected();
                }
            })
        );
    }

    void WsPluginServer::RegisterIpcPid(uint32_t pid) {
        if (pid == 0) {
            return;
        }
        std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
        ipc_allowed_pids_.insert(pid);
        LOGI("IPC (/ipc) registered allowed pid={} (total={})", pid, ipc_allowed_pids_.size());
    }

    bool WsPluginServer::IsIpcPidAllowed(uint32_t pid) {
        std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
        return ipc_allowed_pids_.contains(pid);
    }

    void WsPluginServer::UnregisterIpcPidIfDead(uint32_t pid) {
        if (pid == 0 || IsIpcProcessAlive(pid)) {
            return;
        }
        std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
        if (ipc_allowed_pids_.erase(pid) > 0) {
            LOGI("IPC (/ipc) unregistered dead pid={} (total={})", pid, ipc_allowed_pids_.size());
        }
    }

    void WsPluginServer::SweepDeadIpcPids() {
        // On1Second 每秒驱动,每 5s 真正扫一次;集合很小,OpenProcess 开销可忽略
        if ((++ipc_pid_sweep_ticks_ % 5) != 0) {
            return;
        }
        std::vector<uint32_t> snapshot;
        {
            std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
            snapshot.assign(ipc_allowed_pids_.begin(), ipc_allowed_pids_.end());
        }
        for (const auto pid : snapshot) {
            UnregisterIpcPidIfDead(pid);
        }
    }

    void WsPluginServer::ReportPerformance() {
        const auto media_queue = std::max<std::int64_t>(
            0, GetQueuingMediaMsgCount());
        const auto file_queue = std::max<std::int64_t>(
            0, GetQueuingFtMsgCount());
        const auto queue_depth = static_cast<std::size_t>(
            media_queue + file_queue);
        const auto active_connections = stream_routers_.Size() +
            ft_routers_.Size() + ipc_sessions_.Size() +
            (IsUserProxyConnected() ? 1U : 0U);
        const auto snapshot = transport_performance_.SnapshotAndReset(
            std::chrono::steady_clock::now(), active_connections, queue_depth);
        if (!snapshot) {
            return;
        }
        const auto activity = snapshot->inbound_messages +
            snapshot->outbound_messages + snapshot->dropped_messages +
            snapshot->connected + snapshot->disconnected;
        if (activity == 0 && snapshot->active_connections == 0 &&
            snapshot->queue_depth == 0) {
            return;
        }
        const auto seconds = static_cast<double>(snapshot->window_ms) / 1000.0;
        LOGI("event=transport.window component=net_ws transport=ws "
             "window_ms={} active_connections={} connected={} disconnected={} "
             "inbound_messages={} inbound_bytes={} inbound_mps={:.2f} "
             "outbound_messages={} outbound_bytes={} outbound_mps={:.2f} "
             "bytes_per_second={:.2f} dropped={} queue_depth={} "
             "queue_high_watermark={} outcome=sampled",
             snapshot->window_ms, snapshot->active_connections,
             snapshot->connected, snapshot->disconnected,
             snapshot->inbound_messages, snapshot->inbound_bytes,
             static_cast<double>(snapshot->inbound_messages) / seconds,
             snapshot->outbound_messages, snapshot->outbound_bytes,
             static_cast<double>(snapshot->outbound_messages) / seconds,
             static_cast<double>(snapshot->inbound_bytes +
                                 snapshot->outbound_bytes) / seconds,
             snapshot->dropped_messages, snapshot->queue_depth,
             snapshot->queue_high_watermark);
    }

    void WsPluginServer::AddIpcRouter() {
        // Injected px_gh.dll posts CaptureVideoFrame / IpcCaptureAudioFrame blobs.
        // Decode into owned values and publish through the explicitly injected media ingress.
        auto weak_self = weak_from_this();
        server_->bind(kUrlIpc, websocket::listener<asio2::http_session>{}
            .on("message", [weak_self](std::shared_ptr<asio2::http_session> &sess_ptr, std::string_view data) {
                auto self = weak_self.lock();
                if (!self || self->exiting_ || self->plugin_.expired()) {
                    return;
                }
                self->transport_performance_.ObserveInbound(data.size());
                if (data.size() < sizeof(CaptureBaseMessage)) {
                    return;
                }
                // POD wire format: first field is magic for video frames.
                const auto first_u32 = DecodeWireValue<std::uint32_t>(data);
                if (!first_u32) {
                    return;
                }
                if (*first_u32 == kIpcCaptureVideoFrameMagic) {
                    if (data.size() != sizeof(IpcCaptureVideoFrame)) {
                        LOGE("event=transport.receive component=net_ws "
                             "code=IPC_VIDEO_SIZE_MISMATCH operation=decode_ipc_video "
                             "outcome=dropped recoverable=true bytes={} expected_bytes={}",
                             data.size(), sizeof(IpcCaptureVideoFrame));
                        return;
                    }
                    const auto ipc_msg = DecodeWireValue<IpcCaptureVideoFrame>(data);
                    if (!ipc_msg || ipc_msg->version_ != kIpcCaptureVideoFrameVersion
                        || ipc_msg->type_ != kCaptureVideoFrame) {
                        LOGW("event=transport.receive component=net_ws "
                             "code=IPC_VIDEO_VERSION_MISMATCH operation=decode_ipc_video "
                             "outcome=dropped recoverable=true version={} type={:#x}",
                             ipc_msg->version_, ipc_msg->type_);
                        return;
                    }
                    if (ipc_msg->frame_width_ < 16 || ipc_msg->frame_width_ > 8192
                        || ipc_msg->frame_height_ < 16 || ipc_msg->frame_height_ > 8192) {
                        static std::atomic<uint64_t> s_bad_size{0};
                        const auto n = ++s_bad_size;
                        if (n == 1 || (n % 100) == 0) {
                            LOGW("event=transport.receive component=net_ws "
                                 "code=PIPELINE_INVALID_FRAME operation=decode_ipc_video "
                                 "outcome=dropped recoverable=true width={} height={} count={}",
                                 ipc_msg->frame_width_, ipc_msg->frame_height_, n);
                        }
                        return;
                    }
                    CaptureVideoFrame frame;
                    frame.capture_type_ = ipc_msg->capture_type_;
                    frame.data_length = 0;
                    frame.frame_width_ = ipc_msg->frame_width_;
                    frame.frame_height_ = ipc_msg->frame_height_;
                    frame.frame_index_ = ipc_msg->frame_index_;
                    frame.frame_format_ = ipc_msg->frame_format_;
                    frame.handle_ = ipc_msg->handle_;
                    frame.adapter_uid_ = ipc_msg->adapter_uid_;
                    std::copy(std::begin(ipc_msg->display_name_),
                              std::end(ipc_msg->display_name_),
                              std::begin(frame.display_name_));
                    frame.display_name_[sizeof(frame.display_name_) - 1] = 0;
                    frame.monitor_index_ = ipc_msg->monitor_index_;
                    frame.left_ = ipc_msg->left_;
                    frame.top_ = ipc_msg->top_;
                    frame.right_ = ipc_msg->right_;
                    frame.bottom_ = ipc_msg->bottom_;
                    frame.request_idr_ = ipc_msg->request_idr_ != 0;
                    // raw_image_ stays null — never deserialized from the wire.
                    if (const auto plugin = self->plugin_.lock()) {
                        plugin->SubmitIpcVideoFrame(frame);
                    }
                    return;
                }
                if (*first_u32 == kCaptureVideoFrame) {
                    // Legacy non-POD blob (old dll): refuse it, it used to memcpy a shared_ptr.
                    static std::atomic<uint64_t> s_legacy{0};
                    const auto n = ++s_legacy;
                    if (n == 1 || (n % 100) == 0) {
                        LOGW("event=transport.receive component=net_ws "
                             "code=IPC_LEGACY_VIDEO_REJECTED operation=decode_ipc_video "
                             "outcome=dropped recoverable=true count={}", n);
                    }
                    return;
                }
                if (*first_u32 == kCaptureAudioFrame) {
                    if (data.size() < sizeof(IpcCaptureAudioFrame)) {
                        LOGE("event=transport.receive component=net_ws "
                             "code=IPC_AUDIO_SIZE_MISMATCH operation=decode_ipc_audio "
                             "outcome=dropped recoverable=true bytes={}", data.size());
                        return;
                    }
                    const auto hdr = DecodeWireValue<IpcCaptureAudioFrame>(data);
                    if (!hdr) {
                        return;
                    }
                    const size_t expect = sizeof(IpcCaptureAudioFrame) + hdr->data_length;
                    if (data.size() != expect || hdr->data_length == 0) {
                        LOGE("event=transport.receive component=net_ws "
                             "code=IPC_AUDIO_SIZE_MISMATCH operation=decode_ipc_audio "
                             "outcome=dropped recoverable=true bytes={} expected_bytes={} "
                             "pcm_bytes={}", data.size(), expect, hdr->data_length);
                        return;
                    }
                    auto pcm = Data::From(std::string(
                        data.substr(sizeof(IpcCaptureAudioFrame))));
                    if (!pcm) {
                        LOGE("event=transport.receive component=net_ws "
                             "code=IPC_AUDIO_ALLOCATION_FAILED operation=decode_ipc_audio "
                             "outcome=dropped recoverable=true pcm_bytes={}",
                             hdr->data_length);
                        return;
                    }
                    CaptureAudioFrame frame;
                    frame.frame_index_ = hdr->frame_index_;
                    frame.full_data_ = pcm;
                    frame.samples_ = hdr->samples_;
                    frame.channels_ = hdr->channels_;
                    frame.bits_ = hdr->bits_;
                    static std::atomic<uint64_t> s_audio_rx{0};
                    const auto n = ++s_audio_rx;
                    if (n == 1 || (n % 200) == 0) {
                        LOGI("event=ipc.audio.window component=net_ws count={} frame={} "
                             "sample_rate_hz={} channels={} bits={} bytes={}", n,
                              hdr->frame_index_, hdr->samples_, hdr->channels_, hdr->bits_,
                              hdr->data_length);
                    }
                    if (const auto plugin = self->plugin_.lock()) {
                        plugin->SubmitIpcAudioFrame(frame);
                    }
                    return;
                }
            })
            .on("open", [weak_self](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                const std::string remote_addr(sess_ptr->remote_address().c_str());
                if (!IsLoopbackAddress(remote_addr)) {
                    // /ipc is for the injected dll only; refuse remote peers so they
                    // can neither push forged frames nor sniff the input downlink.
                    LOGW("event=session.admit component=net_ws "
                         "code=SESSION_PEER_NOT_LOCAL operation=validate_ipc_peer "
                         "outcome=rejected recoverable=false peer={} port={}",
                         PrivacyLogId(remote_addr), sess_ptr->remote_port());
                    sess_ptr->stop();
                    return;
                }
                // Pid auth: the dll connects with ?pid=<its own pid>; only pids this
                // render instance wrote hook boot config for (RegisterIpcPid) are accepted.
                // This rejects stale injected games from dead renders, which otherwise
                // reconnect to whatever render starts listening and interleave frames.
                auto query = sess_ptr->get_request().get_query();
                auto params = UrlHelper::ParseQueryString(std::string(query.data(), query.size()));
                uint32_t client_pid = 0;
                if (auto it = params.find("pid"); it != params.end()) {
                    client_pid = static_cast<uint32_t>(std::strtoul(it->second.c_str(), nullptr, 10));
                }
                if (client_pid == 0 || !self->IsIpcPidAllowed(client_pid)) {
                    static std::atomic<uint64_t> s_reject{0};
                    const auto n = ++s_reject;
                    if (n == 1 || (n % 50) == 0) {
                        LOGW("event=session.admit component=net_ws "
                             "code=SESSION_IPC_PID_UNREGISTERED operation=validate_ipc_pid "
                             "outcome=rejected recoverable=false pid={} peer={} port={} count={}",
                             client_pid, PrivacyLogId(remote_addr),
                             sess_ptr->remote_port(), n);
                    }
                    sess_ptr->stop();
                    return;
                }
                sess_ptr->ws_stream().binary(true);
                sess_ptr->set_no_delay(true);
                const auto socket_fd = (uint64_t)sess_ptr->socket().native_handle();
                self->ipc_sessions_.Insert(socket_fd, sess_ptr);
                self->ipc_session_pids_.Insert(socket_fd, client_pid);
                self->transport_performance_.ObserveConnected();
                LOGI("event=session.admit component=net_ws outcome=connected "
                     "route=ipc peer={} port={} fd={} pid={} sessions={}",
                     PrivacyLogId(remote_addr), sess_ptr->remote_port(), socket_fd,
                     client_pid, self->ipc_sessions_.Size());
            })
            .on("close", [weak_self](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                const auto socket_fd = (uint64_t)sess_ptr->socket().native_handle();
                uint32_t pid = self->ipc_session_pids_.TryGet(socket_fd).value_or(0);
                self->ipc_session_pids_.Remove(socket_fd);
                self->ipc_sessions_.Remove(socket_fd);
                self->transport_performance_.ObserveDisconnected();
                // 进程已死才注销;活进程的瞬时断线靠重连恢复,注册保留
                self->UnregisterIpcPidIfDead(pid);
                LOGI("IPC (/ipc) client disconnected fd={} pid={} remaining={}", socket_fd,
                     pid, self->ipc_sessions_.Size());
            })
        );
        LOGI("Registered websocket route: {}", kUrlIpc);
    }

    PxAwaitable<void> WsPluginServer::OpenWebSocketAsync(
        std::weak_ptr<WsPluginServer> owner,
        std::shared_ptr<asio2::http_session> session,
        std::string path,
        std::unordered_map<std::string, std::string> params,
        const std::uint64_t socket_fd) {
        const auto server = owner.lock();
        if (!server || server->exiting_) {
            co_return;
        }
        const auto plugin = server->plugin_;
        auto ticket_result = co_await RedeemWsTicketAsync(plugin, params);
        if (!ticket_result.HasValue()) {
            const auto& error = ticket_result.Error();
            LOGW("event=session.admit component=net_ws code={} "
                 "operation=redeem_ticket outcome=rejected recoverable={} reason={}",
                 error.StableCode(), error.retryable, error.message);
            server->transport_performance_.ObserveDropped();
            RejectWebSocketSession(session, kWsAuthorizationRejectedSignal);
            co_return;
        }
        auto ticket = ticket_result.TakeValue();
        const auto stream_it = params.find("stream_id");
        const auto stream_id = stream_it == params.end()
            ? std::string{} : stream_it->second;
        if (stream_id.empty() || stream_id != ticket.stream_id_) {
            LOGW("event=session.admit component=net_ws code=SESSION_STREAM_MISMATCH "
                 "operation=validate_route outcome=rejected recoverable=false");
            server->transport_performance_.ObserveDropped();
            RejectWebSocketSession(session, kWsAuthorizationRejectedSignal);
            co_return;
        }
        if (path == kUrlFileTransfer &&
            std::find(ticket.permissions_.begin(), ticket.permissions_.end(),
                      "file") == ticket.permissions_.end()) {
            LOGW("event=session.admit component=net_ws code=SESSION_CAPABILITY_DENIED "
                 "operation=file_transfer outcome=rejected recoverable=false");
            server->transport_performance_.ObserveDropped();
            RejectWebSocketSession(session, kWsSessionRejectedSignal);
            co_return;
        }
        const auto binding_id = std::format("ws:{}:{}", stream_id, socket_fd);
        auto admission_result = co_await AdmitWsSessionAsync(
            plugin,
            LogicalSessionGrant{
                .logical_session_id = ticket.logical_session_id_,
                .stream_id = ticket.stream_id_,
                .subject_id = ticket.subject_id_,
                .join_mode = ticket.join_mode_,
                .expires_at_ms = ticket.expires_at_ms_,
                .allow_observer = ticket.allow_observer_,
                .allow_takeover = ticket.allow_takeover_,
            },
            path == kUrlFileTransfer
                ? LogicalSessionTransport::kFileTransfer
                : LogicalSessionTransport::kWs,
            binding_id);
        if (!admission_result.HasValue() ||
            admission_result.Value().code !=
                LogicalSessionAdmissionCode::kAccepted) {
            const bool occupied = admission_result.HasValue() &&
                admission_result.Value().code ==
                    LogicalSessionAdmissionCode::kOccupied;
            const auto code = admission_result.HasValue()
                ? "SESSION_ADMISSION_DENIED"
                : admission_result.Error().StableCode();
            LOGW("event=session.admit component=net_ws code={} "
                 "operation=bind_session outcome=rejected recoverable={} occupied={}",
                 code, !admission_result.HasValue() &&
                           admission_result.Error().retryable,
                 occupied);
            server->transport_performance_.ObserveDropped();
            RejectWebSocketSession(
                session,
                occupied ? kWsSessionOccupiedSignal : kWsSessionRejectedSignal);
            co_return;
        }
        auto admission = admission_result.TakeValue();
        if (!session->is_started()) {
            DispatchCloseLogicalSessionBinding(
                plugin, ticket.logical_session_id_, binding_id);
            co_return;
        }
        session->post_queued_event(
            [owner, plugin, session, path = std::move(path),
             params = std::move(params), ticket = std::move(ticket),
             admission = std::move(admission), binding_id, socket_fd]() mutable {
                const auto active_server = owner.lock();
                if (!active_server || active_server->exiting_ ||
                    !session->is_started()) {
                    DispatchCloseLogicalSessionBinding(
                        plugin, ticket.logical_session_id_, binding_id);
                    return;
                }
                active_server->FinalizeWebSocketOpen(
                    session, path, params, ticket, admission, binding_id,
                    socket_fd);
            });
        co_return;
    }

    void WsPluginServer::FinalizeWebSocketOpen(
        const std::shared_ptr<asio2::http_session>& session,
        const std::string& path,
        const std::unordered_map<std::string, std::string>& params,
        const WsTicketAdmission& ticket,
        const LogicalSessionAdmission&,
        const std::string& binding_id,
        const std::uint64_t socket_fd) {
        const auto plugin = plugin_.lock();
        if (!plugin) {
            DispatchCloseLogicalSessionBinding(
                plugin_, ticket.logical_session_id_, binding_id);
            session->stop();
            return;
        }
        for (const auto& [key, value] : params) {
            static_cast<void>(value);
            LOGI("event=transport.query component=net_ws key={} value=<redacted>",
                 key);
        }
        LOGI("App server {} open", path);
        const auto value_or_empty = [&params](const std::string& key) {
            const auto it = params.find(key);
            return it == params.end() ? std::string{} : it->second;
        };
        const bool only_audio =
            std::atoi(value_or_empty("only_audio").c_str()) == 1;
        const auto visitor_device_id = value_or_empty("visitor_device_id");
        const auto stream_id = value_or_empty("stream_id");
        const bool force_gdi = value_or_empty("force_gdi") == "true";
        bool udp_media = value_or_empty("udp_media") == "1";
        std::string udp_media_association_code;
        if (udp_media && path == kUrlMedia) {
            udp_media_association_code =
                value_or_empty("udp_media_association");
            if (udp_media_association_code.empty()) {
                LOGW("event=transport.route component=net_ws "
                     "code=TRANSPORT_ASSOCIATION_MISSING operation=udp_association "
                     "outcome=websocket_fallback recoverable=true stream={}",
                     PrivacyLogId(stream_id));
                udp_media = false;
            }
            else {
                UpdateUdpMediaAssociation(
                    udp_media_association_code, ticket.logical_session_id_,
                    stream_id, force_gdi, false);
            }
        }
        else if (udp_media) {
            udp_media = false;
        }
        LOGI("Force GDI : {}", force_gdi);
        session->set_no_delay(true);
        if (path == kUrlMedia) {
            const auto event =
                std::make_shared<PxPluginReqParamsBeginStreaming>();
            event->stream_id_ = stream_id;
            event->force_gdi_ = force_gdi;
            plugin->CallbackEvent(event);
            auto router = WsStreamRouter::Make(
                ws_data_, only_audio, visitor_device_id, stream_id);
            router->udp_media_.store(udp_media);
            router->logical_session_id_ = ticket.logical_session_id_;
            router->binding_id_ = binding_id;
            router->clipboard_allowed_.store(std::find(
                ticket.permissions_.begin(), ticket.permissions_.end(),
                "clipboard") != ticket.permissions_.end());
            router->file_allowed_.store(std::find(
                ticket.permissions_.begin(), ticket.permissions_.end(),
                "file") != ticket.permissions_.end());
            router->udp_media_association_code_ =
                udp_media_association_code;
            router->force_gdi_ = force_gdi;
            const auto weak_self = weak_from_this();
            const std::weak_ptr<WsStreamRouter> weak_router = router;
            router->SetUdpMediaFallbackCallback(
                [weak_self, weak_router] {
                    const auto active_server = weak_self.lock();
                    const auto active_router = weak_router.lock();
                    if (!active_server || !active_router) {
                        return;
                    }
                    active_server->UpdateUdpMediaAssociation(
                        active_router->udp_media_association_code_,
                        active_router->logical_session_id_,
                        active_router->stream_id_,
                        active_router->force_gdi_, true);
                });
            stream_routers_.Insert(socket_fd, router);
            NotifyMediaClientConnected(
                router->conn_id_, router->stream_id_, visitor_device_id);
            auto mutable_session = session;
            router->OnOpen(mutable_session);
        }
        else if (path == kUrlFileTransfer) {
            auto router = WsFileTransferRouter::Make(
                ws_data_, only_audio, visitor_device_id, stream_id);
            router->logical_session_id_ = ticket.logical_session_id_;
            router->binding_id_ = binding_id;
            router->file_allowed_.store(std::find(
                ticket.permissions_.begin(), ticket.permissions_.end(),
                "file") != ticket.permissions_.end());
            ft_routers_.Insert(socket_fd, router);
            auto mutable_session = session;
            router->OnOpen(mutable_session);
        }
    }

    void WsPluginServer::AddWebsocketRouter(const std::string &path) {
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
                self->transport_performance_.ObserveInbound(data.size());
                auto socket_fd = fn_get_socket_fd(sess_ptr);
                if (path == kUrlMedia) {
                    self->stream_routers_.VisitAll([=](auto k, std::shared_ptr<WsStreamRouter>& router) mutable {
                        if (socket_fd == k) {
                            router->OnMessage(sess_ptr, socket_fd, data);
                        }
                    });
                }
                else if (path == kUrlFileTransfer) {
                    self->ft_routers_.VisitAll([=](auto k, auto &router) mutable {
                        if (socket_fd == k) {
                            router->OnMessage(sess_ptr, socket_fd, data);
                        }
                    });
                }
            })
            .on("open", [weak_self, path, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_ || !self->async_scope_) {
                    return;
                }
                self->transport_performance_.ObserveConnected();
                const auto query = sess_ptr->get_request().get_query();
                auto params = UrlHelper::ParseQueryString(
                    std::string(query.data(), query.size()));
                const auto session = sess_ptr;
                const auto socket_fd = fn_get_socket_fd(sess_ptr);
                if (!self->async_scope_->Spawn(
                        "ws-session-open",
                        [weak_self, session, path, socket_fd,
                         params = std::move(params)]() mutable {
                            return WsPluginServer::OpenWebSocketAsync(
                                weak_self, session, path, std::move(params),
                                socket_fd);
                        })) {
                    self->transport_performance_.ObserveDropped();
                    RejectWebSocketSession(
                        sess_ptr, kWsAuthorizationRejectedSignal);
                }
            })
            .on("close", [weak_self, path, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                self->transport_performance_.ObserveDisconnected();
                auto socket_fd = fn_get_socket_fd(sess_ptr);
                LOGI("client closed: {}", socket_fd);
                if (path == kUrlMedia) {
                    if (auto opt_val = self->stream_routers_.Remove(socket_fd); opt_val.has_value()) {
                        const auto& val = opt_val.value();
                        self->UpdateUdpMediaAssociation(val->udp_media_association_code_,
                                                        val->logical_session_id_, val->stream_id_, false, true);
                        self->CloseLogicalSessionBinding(val->logical_session_id_, val->binding_id_);
                        self->NotifyMediaClientDisConnected(val->conn_id_, val->stream_id_, val->visitor_device_id_, val->created_timestamp_, val->binding_id_, val->logical_session_id_);
                        LOGI("event=session.close component=net_ws outcome=removed "
                             "device={}", PrivacyLogId(val->visitor_device_id_));
                    }
                }
                else if (path == kUrlFileTransfer) {
                    if (auto removed = self->ft_routers_.Remove(socket_fd);
                        removed.has_value()) {
                        const auto& router = removed.value();
                        self->CloseLogicalSessionBinding(router->logical_session_id_, router->binding_id_);
                        router->OnClose(sess_ptr);
                        self->NotifyMediaClientDisConnected(
                            router->conn_id_, router->stream_id_, router->device_id_,
                            router->created_timestamp_, router->binding_id_, router->logical_session_id_);
                    }
                }
            })
            .on_ping([weak_self](auto &sess_ptr) {

            })
            .on_pong([weak_self](auto &sess_ptr) {

            })
            .on("update", [](std::shared_ptr<asio2::http_session> &sess_ptr) {
                LOGI("update");
            })
        );
    }

    void WsPluginServer::CloseLogicalSessionBinding(
        const std::string& logical_session_id, const std::string& binding_id) {
        if (logical_session_id.empty() || binding_id.empty()) {
            return;
        }
        const auto event = std::make_shared<PxPluginCloseLogicalSessionBindingEvent>();
        event->logical_session_id_ = logical_session_id;
        event->binding_id_ = binding_id;
        if (const auto plugin = plugin_.lock()) {
            plugin->CallbackEvent(event);
        }
    }

    void WsPluginServer::UpdateUdpMediaAssociation(
        const std::string& association_code, const std::string& logical_session_id,
        const std::string& stream_id, const bool force_gdi, const bool revoke) {
        if (association_code.empty()) {
            return;
        }
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto plugin = plugin_.lock();
        const auto updated = plugin && plugin->UpdateUdpAssociation(UdpMediaAssociation{
            .association_code_ = association_code,
            .logical_session_id_ = logical_session_id,
            .stream_id_ = stream_id,
            .expires_at_ms_ = now_ms + std::chrono::seconds(15).count() * 1000,
            .force_gdi_ = force_gdi,
            .revoke_ = revoke,
        });
        if (!updated) {
            LOGE("event=transport.route component=net_ws "
                 "code=MODULE_DEPENDENCY_UNAVAILABLE operation=udp_association "
                 "outcome=failed recoverable=true action={} stream={}",
                 revoke ? "revoke" : "register", PrivacyLogId(stream_id));
            return;
        }
        LOGI("event=transport.route component=net_ws operation=udp_association "
             "outcome=success action={} stream={}",
             revoke ? "revoke" : "register", PrivacyLogId(stream_id));
    }

    void WsPluginServer::AddHttpRouter(const std::string &path,
       std::function<void(const std::string& path, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep)>&& callback) {
        auto weak_self = weak_from_this();
        // bind it
        server_->bind<http::verb::get, http::verb::post>(path, [weak_self, path, callback = std::move(callback)](std::shared_ptr<asio2::http_session> &session_ptr, http::web_request &req, http::web_response &rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            callback(path, session_ptr, req, rep);
        }, aop_log{}); //, http::enable_cache
    }

    void WsPluginServer::AddWebClientRouter() {
        auto web_client_dir = std::filesystem::path(FolderUtil::GetCurrentFolderPath()) / "web_client";
        std::error_code ec;
        if (!std::filesystem::is_directory(web_client_dir, ec)) {
            LOGW("event=module.start component=net_ws "
                 "code=WEB_CLIENT_DIRECTORY_MISSING operation=serve_web_client "
                 "outcome=disabled recoverable=true");
            return;
        }

        // make rep.fill_file() resolve paths relative to the web client dir
        server_->set_root_directory(web_client_dir);

        // serve a file under the web client dir; fallback to index.html for SPA routes
        auto fn_serve = [web_client_dir](http::web_request& req, http::web_response& rep) {
            // url_path: "/web_client" or "/web_client/xxx"
            std::string url_path(req.path());
            std::string rel;
            if (url_path.size() > kUrlWebClient.size()) {
                rel = url_path.substr(kUrlWebClient.size());
                while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) {
                    rel.erase(rel.begin());
                }
            }
            std::error_code fs_ec;
            if (rel.empty() || rel.find("..") != std::string::npos
                || !std::filesystem::is_regular_file(web_client_dir / std::filesystem::path(rel), fs_ec)) {
                rel = "index.html";
            }
            LOGI("event=transport.http component=net_ws operation=serve_web_client "
                 "outcome=success route={} asset={}",
                 PrivacyLogId(url_path), PrivacyLogId(rel));
            // note: asio2 detail::make_filepath appends `path` with operator+= (no separator),
            // so the path must carry a leading '/'
            rep.fill_file(std::filesystem::path("/") / rel);
        };

        auto weak_self = weak_from_this();
        // "/web_client" and "/web_client/" (trailing slashes are stripped by the router)
        server_->bind<http::verb::get>(kUrlWebClient, [weak_self, fn_serve](std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            fn_serve(req, rep);
        }, aop_log{});
        // "/web_client/xxx"
        server_->bind<http::verb::get>(kUrlWebClientWildcard, [weak_self, fn_serve](std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            fn_serve(req, rep);
        }, aop_log{});
        LOGI("event=module.start component=net_ws operation=serve_web_client "
             "outcome=success route=/web_client");
    }

    void WsPluginServer::NotifyMediaClientConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id) {
        auto event = std::make_shared<PxPluginClientConnectedEvent>();
        event->conn_id_ = conn_id;
        event->stream_id_ = stream_id;
        event->conn_type_ = "Direct";
        event->visitor_device_id_ = visitor_device_id;
        event->begin_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        if (const auto plugin = plugin_.lock()) {
            plugin->CallbackEvent(event);
        }
        LOGI("event=session.admit component=net_ws outcome=connected "
             "stream={} device={}",
             PrivacyLogId(stream_id), PrivacyLogId(visitor_device_id));
    }

    void WsPluginServer::NotifyMediaClientDisConnected(
        const std::string& conn_id, const std::string& stream_id,
        const std::string& visitor_device_id, const int64_t begin_timestamp,
        const std::string& connection_instance_id, const std::string& logical_session_id) {
        auto event = std::make_shared<PxPluginClientDisConnectedEvent>();
        event->conn_id_ = conn_id;
        event->connection_instance_id_ = connection_instance_id;
        event->logical_session_id_ = logical_session_id;
        event->stream_id_ = stream_id;
        event->visitor_device_id_ = visitor_device_id;
        event->end_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        event->duration_ = event->end_timestamp_ - begin_timestamp;
        if (const auto plugin = plugin_.lock()) {
            plugin->CallbackEvent(event);
        }
    }

    int64_t WsPluginServer::GetQueuingMediaMsgCount() {
        int64_t count = 0;
        stream_routers_.ApplyAll([&](const auto&, const auto& r) {
            count += r->GetQueuingMsgCount();
        });
        return count;
    }

    int64_t WsPluginServer::GetQueuingFtMsgCount() {
        int64_t count = 0;
        ft_routers_.ApplyAll([&](const auto&, const auto& r) {
            count += r->GetQueuingMsgCount();
        });
        return count;
    }

    std::vector<std::shared_ptr<PxConnectedClientInfo>> WsPluginServer::GetConnectedClientInfo() {
        std::vector<std::shared_ptr<PxConnectedClientInfo>> clients_info;
        stream_routers_.VisitAll([&](const auto&, const std::shared_ptr<WsStreamRouter>& router) {
            std::string device_name;
            {
                std::lock_guard<std::mutex> lock(router->device_name_mtx_);
                device_name = router->device_name_;
            }
            clients_info.push_back(std::make_shared<PxConnectedClientInfo>(PxConnectedClientInfo {
                .device_id_ = router->visitor_device_id_,
                .stream_id_ = router->stream_id_,
                .device_name_ = device_name,
            }));
        });
        return clients_info;
    }

    void WsPluginServer::OnClientHello(const std::shared_ptr<MsgClientHello>& event) {
        stream_routers_.VisitAll([&](const auto&, const std::shared_ptr<WsStreamRouter>& router) {
            LOGI("event=session.hello component=net_ws outcome=received "
                 "event_stream={} router_stream={} device={}",
                 PrivacyLogId(event->stream_id_),
                 PrivacyLogId(router->stream_id_),
                 PrivacyLogId(event->device_name_));
            if (router->stream_id_ == event->stream_id_) {
                {
                    std::lock_guard<std::mutex> lock(router->device_name_mtx_);
                    router->device_name_ = event->device_name_;
                }
                // ClientHello is the application-level acceptance boundary.
                // Refresh the short-lived media-plane association here so the
                // UDP endpoint cannot race ahead of WS admission/configuration.
                if (router->udp_media_.load() && !router->udp_media_association_code_.empty()) {
                    UpdateUdpMediaAssociation(router->udp_media_association_code_,
                                              router->logical_session_id_, router->stream_id_,
                                              router->force_gdi_, false);
                }
            }
        });
    }
}
