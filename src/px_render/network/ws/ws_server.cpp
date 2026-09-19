//
// Created by RGAA on 2024/3/1.
//

#include "ws_server.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

#include "app/app_messages.h"
#include "app/win/ipc_peer_identity.h"
#include "frontend_lease_renewal.h"
#include "http_handler.h"
#include "message_type_ids.h"
#include "network/ws_media_router.h"
#include "px_capture/capture_message.h"
#include "px_capture/capture_text_input.h"
#include "px_common/asio_client_shutdown.h"
#include "px_common/async_operation.h"
#include "px_common/async_result.h"
#include "px_common/async_scope_drain.h"
#include "px_common/data.h"
#include "px_common/file.h"
#include "px_common/folder_util.h"
#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_common/privacy_log.h"
#include "px_common/reliable_websocket_send.h"
#include "px_common/time_util.h"
#include "px_common/url_helper.h"
#include "px_common/ws_control_signal.h"
#include "px_render/architecture/events/render_event.h"
#include "px_render/architecture/runtime/await_callback.h"
#include "px_render/modules/module_ids.h"
#include "ws_callback_workflow.h"
#include "ws_filetransfer_router.h"
#include "ws_realtime_media_queue.h"
#include "ws_stream_router.h"
#include "ws_transport.h"
#include "ws_user_proxy_router.h"

static std::string kUrlMedia = "/media";
static std::string kUrlFileTransfer = "/file/transfer";
static std::string kUrlUserProxy = "/user-proxy";
static std::string kUrlIpc = "/ipc";
static std::string kApiPing = "/api/ping";
static std::string kApiVerifySecurityPassword = "/verify/security/password";
static std::string kApiGetRenderConfiguration = "/get/render/configuration";
static std::string kApiPanelStreamMessage = "/panel/stream/message";
static std::string kApiAllocLocalRtc = "/alloc/local/rtc";
static std::string kUrlWebClient = "/web";
static std::string kUrlWebClientWildcard = "/web/*";

// /ipc carries raw captured frames up and user keyboard/mouse events down.
// It must only ever talk to the injected dll on the same machine.
static bool IsLoopbackAddress(const std::string& addr) {
    return addr == "127.0.0.1" || addr == "::1" || addr == "::ffff:127.0.0.1";
}

template <typename WireValue>
static std::optional<WireValue> DecodeWireValue(const std::string_view bytes) {
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
    HANDLE process_handle =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process_handle) {
        return false;
    }
    DWORD exit_code = 0;
    const bool alive = GetExitCodeProcess(process_handle, &exit_code) &&
                       exit_code == STILL_ACTIVE;
    CloseHandle(process_handle);
    return alive;
}

namespace px {
static int64_t CurrentSystemMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct WsPasswordAdmission {
    std::vector<std::string> permissions_;
    std::string logical_session_id_;
    std::string stream_id_;
    std::string join_mode_;
    std::string subject_id_;
    int64_t expires_at_ms_ = 0;
    bool allow_observer_ = true;
    bool allow_takeover_ = true;
    std::shared_ptr<WebSocketFrontendToken> frontend_token_{};
    std::optional<ConsoleFrontendGrant> console_frontend_grant_{};
    std::string descriptor_session_id_{};
    std::int64_t descriptor_revision_{};
};

static void RejectWebSocketSession(std::shared_ptr<asio2::http_session> session,
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
    const std::weak_ptr<WsTransport>& transport,
    const std::string& logical_session_id, const std::string& binding_id) {
    const auto owner = transport.lock();
    if (!owner || logical_session_id.empty() || binding_id.empty()) {
        return;
    }
    const auto event = std::make_shared<CloseLogicalSessionBindingEvent>();
    event->logical_session_id_ = logical_session_id;
    event->binding_id_ = binding_id;
    owner->EmitEvent(event);
}

static PxAwaitable<PxResult<WsPasswordAdmission>> AuthenticateWsPasswordAsync(
    std::weak_ptr<WsTransport> transport,
    std::unordered_map<std::string, std::string> query_parameters,
    std::string remote_address) {
    const auto owner = transport.lock();
    const auto stream_iterator = query_parameters.find("stream_id");
    const auto nonce_iterator = query_parameters.find("client_nonce");
    const auto password_iterator = query_parameters.find("safety_pwd_md5");
    if (!owner || stream_iterator == query_parameters.end() ||
        stream_iterator->second.empty() ||
        nonce_iterator == query_parameters.end() ||
        nonce_iterator->second.empty() ||
        password_iterator == query_parameters.end() ||
        password_iterator->second.empty()) {
        co_return PxResult<WsPasswordAdmission>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument,
                             "ws_password_auth", "device password is missing"));
    }
    const auto settings = owner->Settings();
    const bool valid_safety_password =
        !settings.device_safety_password.empty() &&
        settings.device_safety_password == password_iterator->second;
    const bool valid_temporary_password =
        !settings.device_random_password.empty() &&
        MD5::Hex(settings.device_random_password) == password_iterator->second;
    if ((!settings.device_safety_password.empty() ||
         !settings.device_random_password.empty()) &&
        !valid_safety_password && !valid_temporary_password) {
        co_return PxResult<WsPasswordAdmission>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kServiceRejected,
                             "ws_password_auth", "device password was rejected",
                             false, "SESSION_PASSWORD_REJECTED"));
    }
    // RDP runtime authorization sends this identifier through Console's strict
    // binding validator, whose portable identifier alphabet is [A-Za-z0-9_-].
    const std::string logical_session_id{
        "password-" + MD5::Hex(stream_iterator->second + "|" +
                               nonce_iterator->second + "|" + remote_address)};
    co_return PxResult<WsPasswordAdmission>::Success(WsPasswordAdmission{
        .permissions_ = {"view", "input", "clipboard", "file", "audio", "rdp"},
        .logical_session_id_ = logical_session_id,
        .stream_id_ = stream_iterator->second,
        .join_mode_ = "control",
        .subject_id_ = "password:" +
                       MD5::Hex(remote_address + "|" + nonce_iterator->second),
        .expires_at_ms_ = 0,
        .allow_observer_ = false,
        .allow_takeover_ = true,
    });
}

static PxAwaitable<PxResult<WsPasswordAdmission>> AuthenticateWebSocketAsync(
    std::weak_ptr<WsTransport> transport,
    std::unordered_map<std::string, std::string> query_parameters,
    std::string remote_address) {
    const auto owner = transport.lock();
    if (!owner) {
        co_return PxResult<WsPasswordAdmission>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kServiceStopped,
                             "ws_frontend_auth", "transport is unavailable",
                             true, "TRANSPORT_UNAVAILABLE"));
    }
    if (!owner->RequiresConsoleFrontendAdmission()) {
        co_return co_await AuthenticateWsPasswordAsync(
            std::move(transport), std::move(query_parameters),
            std::move(remote_address));
    }

    auto descriptor = ConsumeWebSocketFrontendDescriptor(query_parameters);
    if (!descriptor) {
        co_return PxResult<WsPasswordAdmission>::Failure(MakePxAsyncError(
            PxAsyncErrorCode::kInvalidArgument, "ws_frontend_auth",
            "Console frontend descriptor is invalid", false,
            "CONSOLE_FRONTEND_DESCRIPTOR_INVALID"));
    }

    auto admitted = co_await owner->AdmitFrontend(
        ConsoleFrontendAdmissionRequest{
            .request_id = GetUUID(),
            .session_id = descriptor->session_id,
            .revision = descriptor->revision,
            .frontend_token = descriptor->token->Copy(),
        },
        std::chrono::steady_clock::now() + std::chrono::seconds(12));
    if (!admitted.HasValue()) {
        co_return PxResult<WsPasswordAdmission>::Failure(admitted.Error());
    }
    auto grant = admitted.TakeValue();
    const auto settings = owner->Settings();
    if (!IsAcceptedWebSocketFrontendGrant(*descriptor, settings.device_id, grant)) {
        co_return PxResult<WsPasswordAdmission>::Failure(MakePxAsyncError(
            PxAsyncErrorCode::kServiceRejected, "ws_frontend_auth",
            "Console frontend identity was rejected", false,
            "CONSOLE_FRONTEND_IDENTITY_MISMATCH"));
    }
    const bool controller = grant.access_role == "controller";
    co_return PxResult<WsPasswordAdmission>::Success(WsPasswordAdmission{
        .permissions_ = controller
                            ? std::vector<std::string>{"view", "input", "clipboard", "file", "audio", "rdp"}
                            : std::vector<std::string>{"view", "audio"},
        .logical_session_id_ = grant.session_id,
        .stream_id_ = descriptor->stream_id,
        .join_mode_ = controller ? "control" : "observe",
        .subject_id_ = grant.client_type + ":" + grant.session_id,
        .expires_at_ms_ = CurrentSystemMilliseconds() +
                          static_cast<std::int64_t>(grant.valid_for_ms),
        .allow_observer_ = !controller,
        .allow_takeover_ = false,
        .frontend_token_ = descriptor->token,
        .console_frontend_grant_ = std::move(grant),
        .descriptor_session_id_ = descriptor->session_id,
        .descriptor_revision_ = descriptor->revision,
    });
}

static PxAwaitable<PxResult<LogicalSessionAdmission>> AdmitWsSessionAsync(
    std::weak_ptr<WsTransport> weak_transport, LogicalSessionGrant grant,
    const LogicalSessionTransport session_transport, std::string binding_id) {
    const auto logical_session_id = grant.logical_session_id;
    co_return co_await AwaitWsValueCallback<LogicalSessionAdmission>(
        [weak_transport, grant = std::move(grant), session_transport,
         binding_id](std::function<void(LogicalSessionAdmission)> completion) {
            const auto owner = weak_transport.lock();
            if (!owner) {
                return false;
            }
            const auto event = std::make_shared<AdmitLogicalSessionEvent>();
            event->grant_ = grant;
            event->transport_ = session_transport;
            event->binding_id_ = binding_id;
            event->callback_ = std::move(completion);
            owner->EmitEvent(event);
            return true;
        },
        std::chrono::steady_clock::now() + std::chrono::seconds(3),
        "ws_session_admit",
        [weak_transport, logical_session_id,
         binding_id](const LogicalSessionAdmission& admission) {
            if (admission.code == LogicalSessionAdmissionCode::kAccepted) {
                DispatchCloseLogicalSessionBinding(
                    weak_transport, logical_session_id, binding_id);
            }
        });
}

struct aop_log {
    bool before(http::web_request& req, http::web_response& rep) {
        asio2::ignore_unused(rep);
        return true;
    }

    bool after(std::shared_ptr<asio2::http_session>& session_ptr,
               http::web_request& req, http::web_response& rep) {
        ASIO2_ASSERT(
            asio2::get_current_caller<std::shared_ptr<asio2::http_session>>()
                .get() == session_ptr.get());
        asio2::ignore_unused(session_ptr, req, rep);
        return true;
    }
};

// wire 级扫描 px.Message 的 type 字段(field 10, varint, tag=0x50),
// 识别 kVideoFrame(30)/kAudioFrame(40)——与 webrtc_local_transport.cpp 的
// IsMediaFrameMessage 同一做法(不引 protobuf 头,避免 absl 冲突)。
// udp_media 客户端的音视频帧都走 UDP,ws 下发前用它过滤。
static bool IsMediaFrameMessage(const std::shared_ptr<Data>& message) {
    if (!message || message->Size() < 2) {
        return false;
    }
    const std::span<const uint8_t> payload(
        reinterpret_cast<const uint8_t*>(message->MutableBytes().data()),
        static_cast<size_t>(message->Size()));
    size_t payload_offset = 0;
    auto read_varint = [&](uint64_t& decoded_value) -> bool {
        decoded_value = 0;
        int shift = 0;
        while (payload_offset < payload.size() && shift < 64) {
            const uint8_t encoded_byte = payload[payload_offset++];
            decoded_value |= static_cast<uint64_t>(encoded_byte & 0x7F)
                             << shift;
            if (!(encoded_byte & 0x80)) {
                return true;
            }
            shift += 7;
        }
        return false;
    };
    // 逐字段扫描,找到 field 10(type)为止;负载按 wire type 跳过
    while (payload_offset < payload.size()) {
        uint64_t field_tag = 0;
        if (!read_varint(field_tag)) {
            return false;
        }
        const uint32_t field_number = static_cast<uint32_t>(field_tag >> 3);
        const uint32_t wire_type = static_cast<uint32_t>(field_tag & 0x7);
        if (field_number == 10 && wire_type == 0) {
            uint64_t message_type = 0;
            if (!read_varint(message_type)) {
                return false;
            }
            // px_message.proto: kVideoFrame = 30, kAudioFrame = 40
            // udp_media 客户端的音视频都走 UDP,ws 下发前都过滤掉
            return message_type == px::wire::kVideoFrame ||
                   message_type == px::wire::kAudioFrame;
        }
        switch (wire_type) {
            case 0: {
                uint64_t ignored_value{};
                if (!read_varint(ignored_value)) {
                    return false;
                }
                break;
            }
            case 1:
                payload_offset += 8;
                break;
            case 2: {
                uint64_t field_length = 0;
                if (!read_varint(field_length)) {
                    return false;
                }
                payload_offset += static_cast<size_t>(field_length);
                break;
            }
            case 5:
                payload_offset += 4;
                break;
            default:
                return false;  // group 等不支持,视为非媒体帧
        }
        if (payload_offset > payload.size()) {
            return false;
        }
    }
    return false;
}

static std::optional<int> ExtractProtocolMessageType(
    const std::shared_ptr<Data>& message) {
    if (!message) {
        return std::nullopt;
    }
    const auto payload = message->AsString();
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
        const auto field_tag = read_varint();
        if (!field_tag || *field_tag == 0) {
            return std::nullopt;
        }
        const auto field_number = static_cast<uint32_t>(*field_tag >> 3);
        const auto wire_type = static_cast<uint32_t>(*field_tag & 0x7);
        if (field_number == 10 && wire_type == 0) {
            const auto message_type = read_varint();
            return message_type
                       ? std::optional<int>(static_cast<int>(*message_type))
                       : std::nullopt;
        }
        switch (wire_type) {
            case 0:
                if (!read_varint()) return std::nullopt;
                break;
            case 1:
                offset += 8;
                break;
            case 2: {
                const auto length = read_varint();
                if (!length || *length > payload.size() - offset)
                    return std::nullopt;
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

static bool IsClipboardProtocolMessage(const std::shared_ptr<Data>& message) {
    const auto message_type = ExtractProtocolMessageType(message);
    return message_type && (*message_type == px::wire::kClipboardInfo ||
                            *message_type == px::wire::kClipboardInfoResp ||
                            *message_type == px::wire::kClipboardReqAtBegin ||
                            *message_type == px::wire::kClipboardReqBuffer ||
                            *message_type == px::wire::kClipboardReqAtEnd ||
                            *message_type == px::wire::kClipboardRespBuffer);
}

WsServer::WsServer(std::weak_ptr<WsTransport> transport,
                   std::shared_ptr<PxAsyncRuntime> async_runtime,
                   const uint16_t listen_port,
                   const std::uint16_t rdp_proxy_port)
    : transport_(std::move(transport)),
      listen_port_(listen_port),
      rdp_proxy_port_(rdp_proxy_port),
      async_runtime_(std::move(async_runtime)) {}

bool WsServer::Start() {
    if (server_ || async_scope_) {
        Exit();
    }
    exiting_ = false;
    transport_performance_.Reset(std::chrono::steady_clock::now());
    if (!async_runtime_ || async_runtime_->IsStopping()) {
        LOGE(
            "event=module.start component=net_ws "
            "code=ASYNC_RUNTIME_UNAVAILABLE "
            "operation=start_control_workflows outcome=failed "
            "recoverable=false");
        return false;
    }
    async_scope_ = PxAsyncScope::Create(async_runtime_, PxAsyncLane::kControl);
    if (!async_scope_) {
        LOGE(
            "event=module.start component=net_ws "
            "code=ASYNC_SCOPE_CREATE_FAILED "
            "operation=start_control_workflows outcome=failed "
            "recoverable=false");
        return false;
    }
    frontend_lease_renewals_ =
        std::make_shared<WebSocketFrontendLeaseRenewalCoordinator>(
            transport_, async_scope_);
    http_handler_ = std::make_shared<HttpHandler>(transport_, async_scope_);
    auto weak_self = weak_from_this();
    server_ = std::make_shared<asio2::http_server>();
    server_->bind_disconnect(
        [weak_self](std::shared_ptr<asio2::http_session>& session) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            const auto socket_fd =
                static_cast<uint64_t>(session->socket().native_handle());
            // LOGI("client disconnected: {}", socket_fd);
            if (auto removed_stream_router =
                    self->stream_routers_.Remove(socket_fd);
                removed_stream_router.has_value()) {
                const auto& router = removed_stream_router.value();
                if (self->frontend_lease_renewals_) {
                    self->frontend_lease_renewals_->Cancel(router->binding_id_);
                }
                self->UpdateUdpMediaAssociation(
                    router->udp_media_association_code_,
                    router->logical_session_id_, router->stream_id_, false,
                    true);
                self->CloseLogicalSessionBinding(router->logical_session_id_,
                                                 router->binding_id_);
                router->OnClose(session);
                self->NotifyMediaClientDisConnected(
                    router->connection_id_, router->stream_id_,
                    router->visitor_device_id_, router->created_timestamp_,
                    router->binding_id_, router->logical_session_id_);
                LOGI(
                    "event=session.close component=net_ws outcome=removed "
                    "device={}",
                    PrivacyLogId(router->visitor_device_id_));
                LOGI("App server media close, media router size: {}",
                     self->stream_routers_.Size());
            } else if (auto removed = self->ft_routers_.Remove(socket_fd);
                       removed.has_value()) {
                const auto& router = removed.value();
                if (self->frontend_lease_renewals_) {
                    self->frontend_lease_renewals_->Cancel(router->binding_id_);
                }
                self->CloseLogicalSessionBinding(router->logical_session_id_,
                                                 router->binding_id_);
                router->OnClose(session);
                self->NotifyMediaClientDisConnected(
                    router->connection_id_, router->stream_id_,
                    router->device_id_, router->created_timestamp_,
                    router->binding_id_, router->logical_session_id_);
            } else if (self->ipc_sessions_.Remove(socket_fd).has_value()) {
                LOGI("IPC (/ipc) session removed on disconnect, remaining={}",
                     self->ipc_sessions_.Size());
            }
        });

    server_->support_websocket(true);
    ws_data_ = std::make_shared<WsData>(WsData{.transport_ = transport_});

    // auto exe_dir = qApp->applicationDirPath().toStdString();
    // auto pwd_file = std::format("{}/certs/password", exe_dir);
    // auto pwd = (File::OpenForRead(pwd_file))->ReadAllAsString();
    // server_->set_cert_file(
    //     "",
    //     std::format("{}/certs/server.crt", exe_dir),
    //     std::format("{}/certs/server.key", exe_dir),
    //     pwd);

    // if (asio2::get_last_error()) {
    //     LOGE("load cert files failed: {}", asio2::last_error_msg());
    // }
    // else {
    //     LOGE("set cert files success.");
    // }
    // server_->set_verify_mode(asio::ssl::verify_peer);

    // media websocket
    AddWebsocketRouter(kUrlMedia);
    if (rdp_proxy_port_ == 0) {
        AddWebsocketRouter(kUrlFileTransfer);
        // Native host integrations are never reachable on an RDP workspace
        // listener.
        AddIpcRouter();
#if PX_USER_PROXY_ENABLED
        AddUserProxyRouter();
#endif
    }

    // ping
    AddHttpRouter(
        kApiPing,
        [weak_self](const std::string&, std::shared_ptr<asio2::http_session>&,
                    http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandlePing(req, rep);
            }
        });

    // verify security pwd
    AddHttpRouter(
        kApiVerifySecurityPassword,
        [weak_self](const std::string&,
                    std::shared_ptr<asio2::http_session>& session_ptr,
                    http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandleVerifySecurityPassword(req, rep);
            }
        });

    // get render configuration
    AddHttpRouter(
        kApiGetRenderConfiguration,
        [weak_self](const std::string&, std::shared_ptr<asio2::http_session>&,
                    http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandleGetRenderConfiguration(req, rep);
            }
        });

    //
    AddHttpRouter(
        kApiPanelStreamMessage,
        [weak_self](const std::string&, std::shared_ptr<asio2::http_session>&,
                    http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandlePanelStreamMessage(req, rep);
            }
        });

    // kApiAllocLocalRtc
    AddHttpRouter(
        kApiAllocLocalRtc,
        [weak_self](const std::string&,
                    std::shared_ptr<asio2::http_session>& session_ptr,
                    http::web_request& req, http::web_response& rep) {
            if (const auto self = weak_self.lock(); self && !self->exiting_) {
                self->http_handler_->HandleAllocLocalRtc(session_ptr, req, rep);
            }
        });

    // static web client pages (SPA), served from {exe_dir}/web_client
    AddWebClientRouter();

    if (listen_port_ <= 0) {
        LOGE(
            "event=module.start component=net_ws code=WS_LISTEN_PORT_INVALID "
            "operation=start_server outcome=failed recoverable=false port={}",
            listen_port_);
        Exit();
        return false;
    }
    const bool ret = server_->start("0.0.0.0", std::to_string(listen_port_));
    LOGI("App server start result: {}, listen port: {}", ret, listen_port_);
    if (!ret) {
        LOGE(
            "event=module.start component=net_ws code=WS_SERVER_START_FAILED "
            "operation=start_server outcome=failed "
            "recoverable=true port={}",
            listen_port_);
        Exit();
        return false;
    }
    return true;
}

void WsServer::Exit() {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto scope = BeginStop();
    if (!scope) {
        FinishStop();
        return;
    }
    if (scope->IsScopeThread()) {
        LOGI(
            "event=async.scope_drain component=net_ws "
            "operation=stop_control_workflows outcome=deferred "
            "reason=shutdown_requested_from_runtime_thread outstanding={}",
            scope->GetStatistics().outstanding);
        return;
    }
    const auto server = server_;
    const auto adapter_stopped =
        WaitForAsioObjectStoppedBlocking(server, deadline);
    const auto remaining =
        std::max(std::chrono::milliseconds::zero(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     deadline - std::chrono::steady_clock::now()));
    if (!adapter_stopped || !scope->WaitFor(remaining)) {
        LOGE(
            "event=async.scope_drain component=net_ws "
            "code=ASYNC_SCOPE_DRAIN_TIMEOUT "
            "operation=stop_control_workflows outcome=timeout "
            "recoverable=false outstanding={}",
            scope->GetStatistics().outstanding);
        return;
    }
    FinishStop();
}

PxAwaitable<PxResult<void>> WsServer::StopAsync(
    std::shared_ptr<WsServer> owner,
    const std::chrono::steady_clock::time_point deadline) {
    if (!owner) {
        co_return PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "net-ws.stop",
                             "WS server owner is missing"));
    }
    const auto scope = owner->BeginStop();
    const auto adapter_stopped = co_await WaitForAsioObjectStopped(
        owner->server_, deadline, "net-ws.adapter-stop");
    if (!adapter_stopped) {
        co_return adapter_stopped;
    }
    if (scope) {
        const auto drained =
            co_await WaitForAsyncScopeDrain(scope, deadline, "net-ws.stop");
        if (!drained) {
            co_return PxResult<void>::Failure(drained.Error());
        }
    }
    owner->FinishStop();
    co_return PxResult<void>::Success();
}

std::shared_ptr<PxAsyncScope> WsServer::BeginStop() {
    if (exiting_.exchange(true)) {
        return async_scope_;
    }
    const auto server = server_;
    if (server && !server->is_stopped()) {
        server->post([server] {
            server->stop_all_timers();
            server->stop();
        });
    }
    if (async_scope_) {
        async_scope_->BeginStop();
    }
    return async_scope_;
}

void WsServer::FinishStop() {
    if (async_scope_ && async_scope_->GetStatistics().outstanding != 0) {
        return;
    }
    async_scope_.reset();
    http_handler_.reset();
    frontend_lease_renewals_.reset();
    server_.reset();
    ws_data_.reset();
    user_proxy_router_.reset();
    stream_routers_.Clear();
    ft_routers_.Clear();
    ipc_sessions_.Clear();
    ipc_session_pids_.Clear();
}

void WsServer::PostNetMessage(std::shared_ptr<Data> message) {
    if (!message) {
        return;
    }
    const bool is_media_frame = IsMediaFrameMessage(message);
    const bool is_realtime_media =
        ClassifyWsRealtimeMedia(message) != WsRealtimeMediaKind::None;
    const bool is_clipboard_message = IsClipboardProtocolMessage(message);
    stream_routers_.ApplyAll([=](const uint64_t& socket_fd,
                                 const std::shared_ptr<WsStreamRouter>&
                                     router) {
        static_cast<void>(socket_fd);
        if (is_clipboard_message && !router->clipboard_allowed_.load()) {
            transport_performance_.ObserveDropped();
            const auto decision =
                warning_log_gate_.Evaluate("clipboard:" + router->stream_id_,
                                           std::chrono::steady_clock::now());
            if (decision.emit) {
                LOGW(
                    "event=transport.send component=net_ws "
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
        if (is_realtime_media &&
            !router->TryPostRealtimeMediaMessage(message)) {
            transport_performance_.ObserveDropped();
            return;
        }
        if (!is_realtime_media) {
            router->PostBinaryMessage(message);
        }
        transport_performance_.ObserveOutbound(message->Size());
    });
}

void WsServer::UpdateLogicalSessionCapabilities(
    const PxLogicalSessionCapabilityUpdate& update) {
    const bool rdp_allowed = std::ranges::all_of(
        std::array{"rdp", "view", "input", "audio", "clipboard"},
        [&update](std::string_view capability) {
            return std::ranges::find(update.permissions_, capability) !=
                   update.permissions_.end();
        });
    if (rdp_proxy_port_ != 0 && !rdp_allowed) {
        stream_routers_.ApplyAll(
            [&update](const uint64_t&,
                      const std::shared_ptr<WsStreamRouter>& router) {
                if (router && router->stream_id_ == update.stream_id_) {
                    router->RevokeRdp();
                }
            });
        return;
    }
    const bool clipboard_allowed =
        std::find(update.permissions_.begin(), update.permissions_.end(),
                  "clipboard") != update.permissions_.end();
    const bool file_allowed =
        std::find(update.permissions_.begin(), update.permissions_.end(),
                  "file") != update.permissions_.end();
    const bool input_allowed{std::ranges::find(update.permissions_, "input") !=
                             update.permissions_.end()};
    stream_routers_.ApplyAll(
        [&update, input_allowed](
            const std::uint64_t&,
            const std::shared_ptr<WsStreamRouter>& router) {
            if (router && router->stream_id_ == update.stream_id_) {
                router->input_allowed_.store(input_allowed);
            }
        });
    stream_routers_.ApplyAll(
        [&update, clipboard_allowed, file_allowed](
            const uint64_t&, const std::shared_ptr<WsStreamRouter>& router) {
            if (router && router->stream_id_ == update.stream_id_) {
                router->clipboard_allowed_.store(clipboard_allowed);
                router->file_allowed_.store(file_allowed);
            }
        });
    ft_routers_.ApplyAll(
        [&update, file_allowed](
            const uint64_t&,
            const std::shared_ptr<WsFileTransferRouter>& router) {
            if (router && router->stream_id_ == update.stream_id_) {
                router->file_allowed_.store(file_allowed);
            }
        });
}

void WsServer::PostIpcBinaryMessage(std::shared_ptr<Data> message) {
    if (!message || message->Size() <= 0) {
        return;
    }
    const std::string payload = message->AsString();
    int sent = 0;
    ipc_sessions_.ApplyAll(
        [&](const uint64_t&, const std::shared_ptr<asio2::http_session>& sess) {
            if (!sess || !sess->is_started()) {
                return;
            }
            sess->async_send(payload);
            transport_performance_.ObserveOutbound(payload.size());
            ++sent;
        });
    if (sent == 0) {
        transport_performance_.ObserveDropped();
        static std::atomic<uint64_t> s_dropped_message_count{0};
        const auto dropped_message_count = ++s_dropped_message_count;
        if (dropped_message_count == 1 || (dropped_message_count % 100) == 0) {
            LOGW(
                "event=transport.send component=net_ws "
                "code=TRANSPORT_ROUTE_UNAVAILABLE operation=ipc_downlink "
                "outcome=dropped recoverable=true count={} bytes={}",
                dropped_message_count, payload.size());
        }
    } else {
        static std::atomic<uint64_t> s_sent_message_count{0};
        const auto sent_message_count = ++s_sent_message_count;
        if (sent_message_count <= 5 || (sent_message_count % 200) == 0) {
            LOGI("PostIpcBinaryMessage: sent n={} sessions={} bytes={}",
                 sent_message_count, sent, payload.size());
        }
    }
}

bool WsServer::PostIpcBinaryMessageForPid(std::uint32_t pid,
                                          std::shared_ptr<Data> message,
                                          std::function<bool()> authorize) {
    if (exiting_ || !message || !authorize || !IsIpcPidAllowed(pid)) {
        return false;
    }
    std::shared_ptr<asio2::http_session> destination{};
    std::size_t matches{};
    ipc_sessions_.ApplyAll(
        [&](const std::uint64_t& socket,
            const std::shared_ptr<asio2::http_session>& session) {
            if (ipc_session_pids_.TryGet(socket).value_or(0) == pid &&
                session && session->is_started()) {
                destination = session;
                ++matches;
            }
        });
    if (matches != 1 || !destination ||
        FindLoopbackTcpClientPid(destination->remote_port(),
                                 destination->local_port()) != pid) {
        return false;
    }
    PostReliableWebSocketWrite(
        destination, std::move(message), [](bool) {},
        [weak = weak_from_this(),
         session = std::weak_ptr<asio2::http_session>{destination}, pid,
         authorize = std::move(authorize)] {
            const auto server{weak.lock()};
            const auto peer{session.lock()};
            return server && !server->exiting_ && peer && peer->is_started() &&
                   server->IsIpcPidAllowed(pid) &&
                   FindLoopbackTcpClientPid(peer->remote_port(),
                                            peer->local_port()) == pid &&
                   authorize();
        });
    return true;
}

bool WsServer::PostTargetStreamMessage(const std::string& stream_id,
                                       std::shared_ptr<Data> message) {
    bool found_target_stream = false;
    const bool is_media_frame = IsMediaFrameMessage(message);
    const bool is_realtime_media =
        ClassifyWsRealtimeMedia(message) != WsRealtimeMediaKind::None;
    const bool is_clipboard_message = IsClipboardProtocolMessage(message);
    const bool is_voice_audio_frame =
        ExtractProtocolMessageType(message) == px::wire::kVoiceAudioFrame;
    stream_routers_.ApplyAll([=, &found_target_stream](
                                 const uint64_t& socket_fd,
                                 const std::shared_ptr<WsStreamRouter>&
                                     router) {
        static_cast<void>(socket_fd);
        if (stream_id == router->stream_id_ || stream_id.empty()) {
            found_target_stream = true;
            if (is_clipboard_message && !router->clipboard_allowed_.load()) {
                transport_performance_.ObserveDropped();
                const auto decision = warning_log_gate_.Evaluate(
                    "target_clipboard:" + router->stream_id_,
                    std::chrono::steady_clock::now());
                if (decision.emit) {
                    LOGW(
                        "event=transport.send component=net_ws "
                        "code=SESSION_CAPABILITY_DENIED operation=clipboard "
                        "outcome=dropped recoverable=true stream={} "
                        "suppressed={}",
                        PrivacyLogId(router->stream_id_),
                        decision.suppressed_since_last_emit);
                }
                return;
            }
            // udp_media 客户端的媒体帧走 UDP 通道,ws 只发控制消息
            if (is_media_frame && router->udp_media_.load()) {
                return;
            }
            if (is_realtime_media &&
                !router->TryPostRealtimeMediaMessage(message)) {
                transport_performance_.ObserveDropped();
                return;
            }
            if (!is_realtime_media) {
                router->PostBinaryMessage(message);
            }
            transport_performance_.ObserveOutbound(message ? message->Size()
                                                           : 0);
        }
    });
    if (is_voice_audio_frame) {
        static std::atomic_uint64_t voice_frames{};
        const auto count = ++voice_frames;
        if (count == 1U || count % 250U == 0U) {
            LOGI(
                "[VoiceCall] WebSocket downlink queued, frames={}, "
                "target_found={}",
                count, found_target_stream);
        }
    }
    return found_target_stream;
}

FileTransferSendResult WsServer::PostTargetFileTransferMessage(
    const std::string& stream_id, const std::shared_ptr<Data>& message,
    const std::string& connection_instance_id) {
    if (!message) {
        return FileTransferSendResult::TransportError(
            "WebSocket file-transfer payload is empty");
    }
    auto result = FileTransferSendResult::Disconnected(
        "WebSocket file-transfer route was not found");
    ft_routers_.ApplyAll(
        [&](const uint64_t& socket_fd,
            const std::shared_ptr<WsFileTransferRouter>& router) {
            static_cast<void>(socket_fd);
            const bool matches =
                !connection_instance_id.empty()
                    ? connection_instance_id == router->binding_id_
                    : (stream_id == router->stream_id_ || stream_id.empty());
            if (matches) {
                result = router->TryPostBinaryMessage(message);
            }
        });
    if (result.status() == FileTransferSendStatus::kAccepted ||
        result.status() == FileTransferSendStatus::kBusy) {
        if (result.status() == FileTransferSendStatus::kAccepted) {
            transport_performance_.ObserveOutbound(message->Size());
        }
        return result;
    }
    // UDP-direct intentionally multiplexes reliable file traffic over its
    // authenticated WS control binding. This avoids a second redemption of
    // the authenticated control connection and keeps all non-media logic
    // reliable.
    stream_routers_.ApplyAll(
        [&](const uint64_t& socket_fd,
            const std::shared_ptr<WsStreamRouter>& router) {
            static_cast<void>(socket_fd);
            const bool matches =
                !connection_instance_id.empty()
                    ? connection_instance_id == router->binding_id_
                    : (stream_id == router->stream_id_ || stream_id.empty());
            if (matches) {
                result = router->TryPostFileTransferMessage(message);
            }
        });
    if (result.status() == FileTransferSendStatus::kAccepted) {
        transport_performance_.ObserveOutbound(message->Size());
    }
    return result;
}

int WsServer::GetConnectedClientsCount() { return (int)stream_routers_.Size(); }

bool WsServer::IsOnlyAudioClients() {
    bool only_audio_client = true;
    stream_routers_.ApplyAllCond(
        [&](const auto& socket_id, const auto& router) -> bool {
            static_cast<void>(socket_id);
            if (router->enable_video_) {
                only_audio_client = false;
                return true;
            }
            return false;
        });
    return only_audio_client;
}

bool WsServer::IsWorking() { return server_ && server_->is_started(); }

void WsServer::PostUserProxyMessage(std::shared_ptr<Data> message) {
#if PX_USER_PROXY_ENABLED
    if (!message) {
        return;
    }
    if (user_proxy_router_ && user_proxy_router_->IsConnected()) {
        LOGI("PostUserProxyMessage ok, len={}", message->Size());
        user_proxy_router_->PostBinaryMessage(message);
        transport_performance_.ObserveOutbound(message->Size());
    } else {
        LOGW(
            "event=transport.send component=net_ws "
            "code=TRANSPORT_ROUTE_UNAVAILABLE operation=user_proxy "
            "outcome=dropped recoverable=true bytes={}",
            message->Size());
        transport_performance_.ObserveDropped();
    }
#endif
}

bool WsServer::IsUserProxyConnected() {
#if PX_USER_PROXY_ENABLED
    return user_proxy_router_ && user_proxy_router_->IsConnected();
#else
    return false;
#endif
}

void WsServer::AddUserProxyRouter() {
    user_proxy_router_ = WsUserProxyRouter::Make(ws_data_);
    auto weak_self = weak_from_this();
    auto weak_router = std::weak_ptr<WsUserProxyRouter>(user_proxy_router_);
    auto get_socket_fd =
        [](std::shared_ptr<asio2::http_session>& session) -> uint64_t {
        return static_cast<uint64_t>(session->socket().native_handle());
    };
    server_->bind(
        kUrlUserProxy,
        websocket::listener<asio2::http_session>{}
            .on("message",
                [weak_self, weak_router, get_socket_fd](
                    std::shared_ptr<asio2::http_session>& session,
                    std::string_view payload) {
                    if (const auto self = weak_self.lock();
                        self && !self->exiting_) {
                        self->transport_performance_.ObserveInbound(
                            payload.size());
                    }
                    if (auto router = weak_router.lock()) {
                        const auto socket_fd = get_socket_fd(session);
                        router->OnMessage(session, socket_fd, payload);
                    }
                })
            .on("open",
                [weak_self, weak_router,
                 get_socket_fd](std::shared_ptr<asio2::http_session>& session) {
                    auto self = weak_self.lock();
                    if (!self || self->exiting_) {
                        return;
                    }
                    if (auto router = weak_router.lock()) {
                        router->OnOpen(session);
                        self->transport_performance_.ObserveConnected();
                    }
                })
            .on("close", [weak_self, weak_router, get_socket_fd](
                             std::shared_ptr<asio2::http_session>& session) {
                if (auto router = weak_router.lock()) {
                    router->OnClose(session);
                }
                if (const auto self = weak_self.lock()) {
                    self->transport_performance_.ObserveDisconnected();
                }
            }));
}

void WsServer::RegisterIpcPid(uint32_t pid) {
    if (pid == 0) {
        return;
    }
    std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
    ipc_allowed_pids_.insert(pid);
    LOGI("IPC (/ipc) registered allowed pid={} (total={})", pid,
         ipc_allowed_pids_.size());
}

bool WsServer::IsIpcPidAllowed(uint32_t pid) {
    std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
    return ipc_allowed_pids_.contains(pid);
}

void WsServer::UnregisterIpcPidIfDead(uint32_t pid) {
    if (pid == 0 || IsIpcProcessAlive(pid)) {
        return;
    }
    std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
    if (ipc_allowed_pids_.erase(pid) > 0) {
        LOGI("IPC (/ipc) unregistered dead pid={} (total={})", pid,
             ipc_allowed_pids_.size());
    }
}

void WsServer::SweepDeadIpcPids() {
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

void WsServer::ReportPerformance() {
    const auto media_queue =
        std::max<std::int64_t>(0, GetQueuingMediaMsgCount());
    const auto file_queue = std::max<std::int64_t>(0, GetQueuingFtMsgCount());
    const auto queue_depth = static_cast<std::size_t>(media_queue + file_queue);
    const auto active_connections = stream_routers_.Size() +
                                    ft_routers_.Size() + ipc_sessions_.Size() +
                                    (IsUserProxyConnected() ? 1U : 0U);
    const auto snapshot = transport_performance_.SnapshotAndReset(
        std::chrono::steady_clock::now(), active_connections, queue_depth);
    if (!snapshot) {
        return;
    }
    const auto activity = snapshot->inbound_messages +
                          snapshot->outbound_messages +
                          snapshot->dropped_messages + snapshot->connected +
                          snapshot->disconnected;
    if (activity == 0 && snapshot->active_connections == 0 &&
        snapshot->queue_depth == 0) {
        return;
    }
    const auto seconds = static_cast<double>(snapshot->window_ms) / 1000.0;
    LOGI(
        "event=transport.window component=net_ws transport=ws "
        "window_ms={} active_connections={} connected={} disconnected={} "
        "inbound_messages={} inbound_bytes={} inbound_mps={:.2f} "
        "outbound_messages={} outbound_bytes={} outbound_mps={:.2f} "
        "bytes_per_second={:.2f} dropped={} queue_depth={} "
        "queue_high_watermark={} outcome=sampled",
        snapshot->window_ms, snapshot->active_connections, snapshot->connected,
        snapshot->disconnected, snapshot->inbound_messages,
        snapshot->inbound_bytes,
        static_cast<double>(snapshot->inbound_messages) / seconds,
        snapshot->outbound_messages, snapshot->outbound_bytes,
        static_cast<double>(snapshot->outbound_messages) / seconds,
        static_cast<double>(snapshot->inbound_bytes +
                            snapshot->outbound_bytes) /
            seconds,
        snapshot->dropped_messages, snapshot->queue_depth,
        snapshot->queue_high_watermark);
}

void WsServer::AddIpcRouter() {
    // Injected px_gh.dll posts CaptureVideoFrame / IpcCaptureAudioFrame blobs.
    // Decode into owned values and publish through the explicitly injected
    // media ingress.
    auto weak_self = weak_from_this();
    server_->bind(
        kUrlIpc,
        websocket::listener<asio2::http_session>{}
            .on("message",
                [weak_self](std::shared_ptr<asio2::http_session>& session,
                            std::string_view payload) {
                    auto self = weak_self.lock();
                    if (!self || self->exiting_ || self->transport_.expired()) {
                        return;
                    }
                    self->transport_performance_.ObserveInbound(payload.size());
                    if (const auto capture_reply{
                            DecodeCaptureTextReply(payload)}) {
                        const auto socket{static_cast<std::uint64_t>(
                            session->socket().native_handle())};
                        const auto pid{self->ipc_session_pids_.TryGet(socket)};
                        if (pid && FindLoopbackTcpClientPid(
                                       session->remote_port(),
                                       session->local_port()) == *pid) {
                            if (const auto transport{self->transport_.lock()}) {
                                const auto event{
                                    std::make_shared<GameTextReplyEvent>()};
                                event->authenticated_pid = *pid;
                                event->reply = *capture_reply;
                                transport->EmitEvent(event);
                            }
                        }
                        return;
                    }
                    if (payload.size() < sizeof(CaptureBaseMessage)) {
                        return;
                    }
                    // POD wire format: first field is magic for video frames.
                    const auto message_magic_or_type =
                        DecodeWireValue<std::uint32_t>(payload);
                    if (!message_magic_or_type) {
                        return;
                    }
                    if (*message_magic_or_type == kIpcCaptureVideoFrameMagic) {
                        if (payload.size() != sizeof(IpcCaptureVideoFrame)) {
                            LOGE(
                                "event=transport.receive component=net_ws "
                                "code=IPC_VIDEO_SIZE_MISMATCH "
                                "operation=decode_ipc_video "
                                "outcome=dropped recoverable=true bytes={} "
                                "expected_bytes={}",
                                payload.size(), sizeof(IpcCaptureVideoFrame));
                            return;
                        }
                        const auto video_header =
                            DecodeWireValue<IpcCaptureVideoFrame>(payload);
                        if (!video_header ||
                            video_header->version_ !=
                                kIpcCaptureVideoFrameVersion ||
                            video_header->type_ != kCaptureVideoFrame) {
                            LOGW(
                                "event=transport.receive component=net_ws "
                                "code=IPC_VIDEO_VERSION_MISMATCH "
                                "operation=decode_ipc_video "
                                "outcome=dropped recoverable=true version={} "
                                "type={:#x}",
                                video_header->version_, video_header->type_);
                            return;
                        }
                        if (video_header->frame_width_ < 16 ||
                            video_header->frame_width_ > 8192 ||
                            video_header->frame_height_ < 16 ||
                            video_header->frame_height_ > 8192) {
                            static std::atomic<uint64_t>
                                s_invalid_frame_size_count{0};
                            const auto invalid_frame_size_count =
                                ++s_invalid_frame_size_count;
                            if (invalid_frame_size_count == 1 ||
                                (invalid_frame_size_count % 100) == 0) {
                                LOGW(
                                    "event=transport.receive "
                                    "component=net_ws "
                                    "code=PIPELINE_INVALID_FRAME "
                                    "operation=decode_ipc_video "
                                    "outcome=dropped "
                                    "recoverable=true width={} "
                                    "height={} count={}",
                                    video_header->frame_width_,
                                    video_header->frame_height_,
                                    invalid_frame_size_count);
                            }
                            return;
                        }
                        CaptureVideoFrame video_frame;
                        video_frame.capture_type_ = video_header->capture_type_;
                        video_frame.data_length = 0;
                        video_frame.frame_width_ = video_header->frame_width_;
                        video_frame.frame_height_ = video_header->frame_height_;
                        video_frame.frame_index_ = video_header->frame_index_;
                        video_frame.frame_format_ = video_header->frame_format_;
                        video_frame.handle_ = video_header->handle_;
                        video_frame.adapter_uid_ = video_header->adapter_uid_;
                        std::copy(std::begin(video_header->display_name_),
                                  std::end(video_header->display_name_),
                                  std::begin(video_frame.display_name_));
                        video_frame
                            .display_name_[sizeof(video_frame.display_name_) -
                                           1] = 0;
                        video_frame.monitor_index_ =
                            video_header->monitor_index_;
                        video_frame.left_ = video_header->left_;
                        video_frame.top_ = video_header->top_;
                        video_frame.right_ = video_header->right_;
                        video_frame.bottom_ = video_header->bottom_;
                        video_frame.request_idr_ =
                            video_header->request_idr_ != 0;
                        // raw_image_ stays null — never deserialized from the
                        // wire.
                        if (const auto transport = self->transport_.lock()) {
                            transport->SubmitIpcVideoFrame(video_frame);
                        }
                        return;
                    }
                    if (*message_magic_or_type == kCaptureVideoFrame) {
                        // Legacy non-POD blob (old dll): refuse it, it used to
                        // memcpy a shared_ptr.
                        static std::atomic<uint64_t> s_legacy_frame_count{0};
                        const auto legacy_frame_count = ++s_legacy_frame_count;
                        if (legacy_frame_count == 1 ||
                            (legacy_frame_count % 100) == 0) {
                            LOGW(
                                "event=transport.receive "
                                "component=net_ws "
                                "code=IPC_LEGACY_VIDEO_REJECTED "
                                "operation=decode_ipc_video "
                                "outcome=dropped recoverable=true "
                                "count={}",
                                legacy_frame_count);
                        }
                        return;
                    }
                    if (*message_magic_or_type == kCaptureAudioFrame) {
                        if (payload.size() < sizeof(IpcCaptureAudioFrame)) {
                            LOGE(
                                "event=transport.receive component=net_ws "
                                "code=IPC_AUDIO_SIZE_MISMATCH "
                                "operation=decode_ipc_audio "
                                "outcome=dropped recoverable=true bytes={}",
                                payload.size());
                            return;
                        }
                        const auto audio_header =
                            DecodeWireValue<IpcCaptureAudioFrame>(payload);
                        if (!audio_header) {
                            return;
                        }
                        const size_t expected_size =
                            sizeof(IpcCaptureAudioFrame) +
                            audio_header->data_length;
                        if (payload.size() != expected_size ||
                            audio_header->data_length == 0) {
                            LOGE(
                                "event=transport.receive component=net_ws "
                                "code=IPC_AUDIO_SIZE_MISMATCH "
                                "operation=decode_ipc_audio "
                                "outcome=dropped recoverable=true bytes={} "
                                "expected_bytes={} "
                                "pcm_bytes={}",
                                payload.size(), expected_size,
                                audio_header->data_length);
                            return;
                        }
                        auto pcm_payload = Data::From(std::string(
                            payload.substr(sizeof(IpcCaptureAudioFrame))));
                        if (!pcm_payload) {
                            LOGE(
                                "event=transport.receive component=net_ws "
                                "code=IPC_AUDIO_ALLOCATION_FAILED "
                                "operation=decode_ipc_audio "
                                "outcome=dropped recoverable=true pcm_bytes={}",
                                audio_header->data_length);
                            return;
                        }
                        CaptureAudioFrame audio_frame;
                        audio_frame.frame_index_ = audio_header->frame_index_;
                        audio_frame.full_data_ = pcm_payload;
                        audio_frame.samples_ = audio_header->samples_;
                        audio_frame.channels_ = audio_header->channels_;
                        audio_frame.bits_ = audio_header->bits_;
                        static std::atomic<uint64_t>
                            s_received_audio_frame_count{0};
                        const auto received_audio_frame_count =
                            ++s_received_audio_frame_count;
                        if (received_audio_frame_count == 1 ||
                            (received_audio_frame_count % 200) == 0) {
                            LOGI(
                                "event=ipc.audio.window "
                                "component=net_ws count={} frame={} "
                                "sample_rate_hz={} channels={} "
                                "bits={} bytes={}",
                                received_audio_frame_count,
                                audio_header->frame_index_,
                                audio_header->samples_, audio_header->channels_,
                                audio_header->bits_, audio_header->data_length);
                        }
                        if (const auto transport = self->transport_.lock()) {
                            transport->SubmitIpcAudioFrame(audio_frame);
                        }
                        return;
                    }
                })
            .on("open",
                [weak_self](std::shared_ptr<asio2::http_session>& session) {
                    auto self = weak_self.lock();
                    if (!self || self->exiting_) {
                        return;
                    }
                    const std::string remote_address(
                        session->remote_address().c_str());
                    if (!IsLoopbackAddress(remote_address)) {
                        // /ipc is for the injected dll only; refuse remote
                        // peers so they can neither push forged frames nor
                        // sniff the input downlink.
                        LOGW(
                            "event=session.admit component=net_ws "
                            "code=SESSION_PEER_NOT_LOCAL "
                            "operation=validate_ipc_peer "
                            "outcome=rejected recoverable=false peer={} "
                            "port={}",
                            PrivacyLogId(remote_address),
                            session->remote_port());
                        session->stop();
                        return;
                    }
                    // Pid auth: the dll connects with ?pid=<its own pid>; only
                    // pids this render instance wrote hook boot config for
                    // (RegisterIpcPid) are accepted. This rejects stale
                    // injected games from dead renders, which otherwise
                    // reconnect to whatever render starts listening and
                    // interleave frames.
                    auto query = session->get_request().get_query();
                    auto query_parameters = UrlHelper::ParseQueryString(
                        std::string(query.data(), query.size()));
                    uint32_t client_pid = 0;
                    if (auto parameter_iterator = query_parameters.find("pid");
                        parameter_iterator != query_parameters.end()) {
                        client_pid = static_cast<uint32_t>(std::strtoul(
                            parameter_iterator->second.c_str(), nullptr, 10));
                    }
                    if (client_pid == 0 || !self->IsIpcPidAllowed(client_pid) ||
                        FindLoopbackTcpClientPid(session->remote_port(),
                                                 session->local_port()) !=
                            client_pid) {
                        static std::atomic<uint64_t>
                            s_rejected_ipc_session_count{0};
                        const auto rejected_ipc_session_count =
                            ++s_rejected_ipc_session_count;
                        if (rejected_ipc_session_count == 1 ||
                            (rejected_ipc_session_count % 50) == 0) {
                            LOGW(
                                "event=session.admit "
                                "component=net_ws "
                                "code=SESSION_IPC_PID_UNREGISTERED "
                                "operation=validate_ipc_pid "
                                "outcome=rejected recoverable=false "
                                "pid={} peer={} port={} count={}",
                                client_pid, PrivacyLogId(remote_address),
                                session->remote_port(),
                                rejected_ipc_session_count);
                        }
                        session->stop();
                        return;
                    }
                    session->ws_stream().binary(true);
                    session->set_no_delay(true);
                    const auto socket_fd = static_cast<uint64_t>(
                        session->socket().native_handle());
                    self->ipc_sessions_.Insert(socket_fd, session);
                    self->ipc_session_pids_.Insert(socket_fd, client_pid);
                    self->transport_performance_.ObserveConnected();
                    LOGI(
                        "event=session.admit component=net_ws "
                        "outcome=connected "
                        "route=ipc peer={} port={} fd={} pid={} sessions={}",
                        PrivacyLogId(remote_address), session->remote_port(),
                        socket_fd, client_pid, self->ipc_sessions_.Size());
                })
            .on("close", [weak_self](
                             std::shared_ptr<asio2::http_session>& session) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                const auto socket_fd =
                    static_cast<uint64_t>(session->socket().native_handle());
                uint32_t pid =
                    self->ipc_session_pids_.TryGet(socket_fd).value_or(0);
                static_cast<void>(self->ipc_session_pids_.Remove(socket_fd));
                static_cast<void>(self->ipc_sessions_.Remove(socket_fd));
                self->transport_performance_.ObserveDisconnected();
                // 进程已死才注销;活进程的瞬时断线靠重连恢复,注册保留
                self->UnregisterIpcPidIfDead(pid);
                LOGI("IPC (/ipc) client disconnected fd={} pid={} remaining={}",
                     socket_fd, pid, self->ipc_sessions_.Size());
            }));
    LOGI("Registered websocket route: {}", kUrlIpc);
}

PxAwaitable<void> WsServer::OpenWebSocketAsync(
    std::weak_ptr<WsServer> owner, std::shared_ptr<asio2::http_session> session,
    std::string path,
    std::unordered_map<std::string, std::string> query_parameters,
    const std::uint64_t socket_fd) {
    const auto server = owner.lock();
    if (!server || server->exiting_) {
        co_return;
    }
    const auto transport = server->transport_;
    auto authentication_result = co_await AuthenticateWebSocketAsync(
        transport, query_parameters, session->remote_address());
    if (!authentication_result.HasValue()) {
        const auto& error = authentication_result.Error();
        LOGW(
            "event=session.admit component=net_ws code={} "
            "operation=frontend_auth outcome=rejected recoverable={} reason={}",
            error.StableCode(), error.retryable, error.message);
        server->transport_performance_.ObserveDropped();
        RejectWebSocketSession(session, kWsAuthorizationRejectedSignal);
        co_return;
    }
    auto authentication = authentication_result.TakeValue();
    query_parameters.erase("frontend_token");
    const bool rdp_requested =
        query_parameters.contains("rdp") && query_parameters.at("rdp") == "1";
    if (server->rdp_proxy_port_ != 0) {
        if (!rdp_requested || path != kUrlMedia ||
            query_parameters.contains("udp_media") ||
            !std::ranges::all_of(
                std::array{"rdp", "view", "input", "audio", "clipboard"},
                [&authentication](std::string_view capability) {
                    return std::ranges::find(authentication.permissions_,
                                             capability) !=
                           authentication.permissions_.end();
                })) {
            RejectWebSocketSession(session, kWsAuthorizationRejectedSignal);
            co_return;
        }
        authentication.allow_observer_ = false;
        authentication.allow_takeover_ = false;
        if (authentication.join_mode_ != "control") {
            RejectWebSocketSession(session, kWsSessionRejectedSignal);
            co_return;
        }
    } else if (rdp_requested) {
        RejectWebSocketSession(session, kWsSessionRejectedSignal);
        co_return;
    }
    const auto stream_iterator = query_parameters.find("stream_id");
    const auto stream_id = stream_iterator == query_parameters.end()
                               ? std::string{}
                               : stream_iterator->second;
    if (stream_id.empty() || stream_id != authentication.stream_id_) {
        LOGW(
            "event=session.admit component=net_ws code=SESSION_STREAM_MISMATCH "
            "operation=validate_route outcome=rejected recoverable=false");
        server->transport_performance_.ObserveDropped();
        RejectWebSocketSession(session, kWsAuthorizationRejectedSignal);
        co_return;
    }
    if (path == kUrlFileTransfer &&
        std::find(authentication.permissions_.begin(),
                  authentication.permissions_.end(),
                  "file") == authentication.permissions_.end()) {
        LOGW(
            "event=session.admit component=net_ws "
            "code=SESSION_CAPABILITY_DENIED "
            "operation=file_transfer outcome=rejected recoverable=false");
        server->transport_performance_.ObserveDropped();
        RejectWebSocketSession(session, kWsSessionRejectedSignal);
        co_return;
    }
    const auto binding_id = std::format("ws:{}:{}", stream_id, socket_fd);
    const LogicalSessionGrant logical_grant{
        .logical_session_id = authentication.logical_session_id_,
        .stream_id = authentication.stream_id_,
        .subject_id = authentication.subject_id_,
        .join_mode = authentication.join_mode_,
        .expires_at_ms = authentication.expires_at_ms_,
        .allow_observer = authentication.allow_observer_,
        .allow_takeover = authentication.allow_takeover_,
        .input_allowed =
            std::ranges::find(authentication.permissions_, "input") !=
            authentication.permissions_.end(),
    };
    auto admission_result = co_await AdmitWsSessionAsync(
        transport, logical_grant,
        path == kUrlFileTransfer ? LogicalSessionTransport::kFileTransfer
                                 : LogicalSessionTransport::kWs,
        binding_id);
    if (!admission_result.HasValue() ||
        admission_result.Value().code !=
            LogicalSessionAdmissionCode::kAccepted) {
        const bool occupied = admission_result.HasValue() &&
                              admission_result.Value().code ==
                                  LogicalSessionAdmissionCode::kOccupied;
        const bool remote_access_disabled =
            admission_result.HasValue() &&
            admission_result.Value().code ==
                LogicalSessionAdmissionCode::kRemoteAccessDisabled;
        const auto code = admission_result.HasValue()
                              ? "SESSION_ADMISSION_DENIED"
                              : admission_result.Error().StableCode();
        LOGW(
            "event=session.admit component=net_ws code={} "
            "operation=bind_session outcome=rejected recoverable={} "
            "occupied={}",
            code,
            !admission_result.HasValue() && admission_result.Error().retryable,
            occupied);
        server->transport_performance_.ObserveDropped();
        RejectWebSocketSession(session,
                               remote_access_disabled
                                   ? kWsRemoteAccessDisabledSignal
                                   : (occupied ? kWsSessionOccupiedSignal
                                               : kWsSessionRejectedSignal));
        co_return;
    }
    auto admission = admission_result.TakeValue();
    if (!session->is_started()) {
        DispatchCloseLogicalSessionBinding(
            transport, authentication.logical_session_id_, binding_id);
        co_return;
    }
    if (authentication.console_frontend_grant_ &&
        authentication.frontend_token_ && server->frontend_lease_renewals_) {
        const std::weak_ptr<asio2::http_session> weak_session{session};
        server->frontend_lease_renewals_->Start(
            WebSocketFrontendLeaseIdentity{
                .expected_grant = *authentication.console_frontend_grant_,
                .logical_grant = logical_grant,
                .descriptor_session_id = authentication.descriptor_session_id_,
                .descriptor_revision = authentication.descriptor_revision_,
                .binding_id = binding_id,
                .terminate_transport = [weak_session] {
                    if (const auto active_session = weak_session.lock()) {
                        active_session->post_queued_event(
                            [active_session] { active_session->stop(); });
                    }
                },
            },
            authentication.frontend_token_,
            authentication.console_frontend_grant_->valid_for_ms);
    }
    session->post_queued_event(
        [owner, transport, session, path = std::move(path),
         query_parameters = std::move(query_parameters),
         authentication = std::move(authentication),
         admission = std::move(admission), binding_id, socket_fd]() mutable {
            const auto active_server = owner.lock();
            if (!active_server || active_server->exiting_ ||
                !session->is_started()) {
                if (active_server && active_server->frontend_lease_renewals_) {
                    active_server->frontend_lease_renewals_->Cancel(binding_id);
                }
                DispatchCloseLogicalSessionBinding(
                    transport, authentication.logical_session_id_, binding_id);
                return;
            }
            active_server->FinalizeWebSocketOpen(
                session, path, query_parameters, authentication, admission,
                binding_id, socket_fd);
        });
    co_return;
}

void WsServer::FinalizeWebSocketOpen(
    const std::shared_ptr<asio2::http_session>& session,
    const std::string& path,
    const std::unordered_map<std::string, std::string>& query_parameters,
    const WsPasswordAdmission& authentication, const LogicalSessionAdmission&,
    const std::string& binding_id, const std::uint64_t socket_fd) {
    const auto transport = transport_.lock();
    if (!transport) {
        DispatchCloseLogicalSessionBinding(
            transport_, authentication.logical_session_id_, binding_id);
        session->stop();
        return;
    }
    for (const auto& [key, value] : query_parameters) {
        static_cast<void>(value);
        LOGI("event=transport.query component=net_ws key={} value=<redacted>",
             key);
    }
    LOGI("App server {} open", path);
    const auto value_or_empty = [&query_parameters](const std::string& key) {
        const auto parameter_iterator = query_parameters.find(key);
        return parameter_iterator == query_parameters.end()
                   ? std::string{}
                   : parameter_iterator->second;
    };
    const bool only_audio =
        std::atoi(value_or_empty("only_audio").c_str()) == 1;
    const auto visitor_device_id = value_or_empty("visitor_device_id");
    const auto stream_id = value_or_empty("stream_id");
    const bool force_gdi = value_or_empty("force_gdi") == "true";
    bool udp_media = value_or_empty("udp_media") == "1";
    std::string udp_media_association_code;
    if (udp_media && path == kUrlMedia) {
        udp_media_association_code = value_or_empty("udp_media_association");
        if (udp_media_association_code.empty()) {
            LOGW(
                "event=transport.route component=net_ws "
                "code=TRANSPORT_ASSOCIATION_MISSING operation=udp_association "
                "outcome=websocket_fallback recoverable=true stream={}",
                PrivacyLogId(stream_id));
            udp_media = false;
        } else {
            UpdateUdpMediaAssociation(udp_media_association_code,
                                      authentication.logical_session_id_,
                                      stream_id, force_gdi, false);
        }
    } else if (udp_media) {
        udp_media = false;
    }
    LOGI("Force GDI : {}", force_gdi);
    session->set_no_delay(true);
    if (path == kUrlMedia) {
        if (rdp_proxy_port_ != 0) {
            const auto generation =
                rdp_frontend_.Acquire(authentication.logical_session_id_);
            if (!generation) {
                DispatchCloseLogicalSessionBinding(
                    transport_, authentication.logical_session_id_, binding_id);
                RejectWebSocketSession(session, kWsSessionOccupiedSignal);
                return;
            }
            const auto router = WsStreamRouter::Make(
                ws_data_, false, visitor_device_id, stream_id);
            router->logical_session_id_ = authentication.logical_session_id_;
            router->binding_id_ = binding_id;
            auto mutable_session = session;
            router->OnOpen(mutable_session);
            const auto weak = weak_from_this();
            const bool started = router->StartRdp(
                async_runtime_->Executor(PxAsyncLane::kWorker), rdp_proxy_port_,
                [weak, generation = *generation] {
                    if (const auto self = weak.lock()) {
                        self->rdp_frontend_.Release(generation);
                    }
                },
                [weak, socket_fd,
                 weak_router = std::weak_ptr<WsStreamRouter>{router},
                 weak_session = std::weak_ptr<asio2::http_session>{session}] {
                    const auto self = weak.lock();
                    const auto route = weak_router.lock();
                    auto client = weak_session.lock();
                    if (!self || self->exiting_ || !route || !client) {
                        return;
                    }
                    // A late callback must not remove a new route if Windows
                    // has reused the socket value. RemoveIf is atomic with
                    // insertion.
                    const auto removed = self->stream_routers_.RemoveIf(
                        socket_fd, [&route](const auto& current) {
                            return current == route;
                        });
                    if (!removed) {
                        return;
                    }
                    route->OnClose(client);
                    self->CloseLogicalSessionBinding(route->logical_session_id_,
                                                     route->binding_id_);
                    self->NotifyMediaClientDisConnected(
                        route->connection_id_, route->stream_id_,
                        route->visitor_device_id_, route->created_timestamp_,
                        route->binding_id_, route->logical_session_id_);
                });
            stream_routers_.Insert(socket_fd, router);
            NotifyMediaClientConnected(router->connection_id_, stream_id,
                                       visitor_device_id, router->logical_session_id_);
            if (!started) {
                session->stop();
            }
            return;
        }
        const auto event =
            std::make_shared<StreamingParametersRequestedEvent>();
        event->stream_id_ = stream_id;
        event->force_gdi_ = force_gdi;
        transport->EmitEvent(event);
        auto router = WsStreamRouter::Make(ws_data_, only_audio,
                                           visitor_device_id, stream_id);
        router->udp_media_.store(udp_media);
        router->logical_session_id_ = authentication.logical_session_id_;
        router->binding_id_ = binding_id;
        router->clipboard_allowed_.store(
            std::find(authentication.permissions_.begin(),
                      authentication.permissions_.end(),
                      "clipboard") != authentication.permissions_.end());
        router->file_allowed_.store(
            std::find(authentication.permissions_.begin(),
                      authentication.permissions_.end(),
                      "file") != authentication.permissions_.end());
        router->input_allowed_.store(
            std::find(authentication.permissions_.begin(),
                      authentication.permissions_.end(),
                      "input") != authentication.permissions_.end());
        router->udp_media_association_code_ = udp_media_association_code;
        router->force_gdi_ = force_gdi;
        const auto weak_self = weak_from_this();
        const std::weak_ptr<WsStreamRouter> weak_router = router;
        router->SetUdpMediaFallbackCallback([weak_self, weak_router] {
            const auto active_server = weak_self.lock();
            const auto active_router = weak_router.lock();
            if (!active_server || !active_router) {
                return;
            }
            active_server->UpdateUdpMediaAssociation(
                active_router->udp_media_association_code_,
                active_router->logical_session_id_, active_router->stream_id_,
                active_router->force_gdi_, true);
        });
        stream_routers_.Insert(socket_fd, router);
        NotifyMediaClientConnected(router->connection_id_, router->stream_id_,
                                   visitor_device_id, router->logical_session_id_);
        auto mutable_session = session;
        router->OnOpen(mutable_session);
    } else if (path == kUrlFileTransfer) {
        auto router = WsFileTransferRouter::Make(ws_data_, only_audio,
                                                 visitor_device_id, stream_id);
        router->logical_session_id_ = authentication.logical_session_id_;
        router->binding_id_ = binding_id;
        router->file_allowed_.store(
            std::find(authentication.permissions_.begin(),
                      authentication.permissions_.end(),
                      "file") != authentication.permissions_.end());
        ft_routers_.Insert(socket_fd, router);
        auto mutable_session = session;
        router->OnOpen(mutable_session);
    }
}

void WsServer::AddWebsocketRouter(const std::string& path) {
    auto weak_self = weak_from_this();
    auto get_socket_fd =
        [](std::shared_ptr<asio2::http_session>& session) -> uint64_t {
        auto& socket = session->socket();
        return static_cast<uint64_t>(socket.native_handle());
    };
    server_->bind(
        path,
        websocket::listener<asio2::http_session>{}
            .on("message",
                [weak_self, path, get_socket_fd](
                    std::shared_ptr<asio2::http_session>& session,
                    std::string_view payload) {
                    auto self = weak_self.lock();
                    if (!self || self->exiting_) {
                        return;
                    }
                    self->transport_performance_.ObserveInbound(payload.size());
                    const auto socket_fd = get_socket_fd(session);
                    if (path == kUrlMedia) {
                        const auto router =
                            self->stream_routers_.TryGet(socket_fd);
                        if (router && *router) {
                            (*router)->OnMessage(session, socket_fd, payload);
                        }
                    } else if (path == kUrlFileTransfer) {
                        const auto router = self->ft_routers_.TryGet(socket_fd);
                        if (router && *router) {
                            (*router)->OnMessage(session, socket_fd, payload);
                        }
                    }
                })
            .on("open",
                [weak_self, path,
                 get_socket_fd](std::shared_ptr<asio2::http_session>& session) {
                    const auto self = weak_self.lock();
                    if (!self || self->exiting_ || !self->async_scope_) {
                        return;
                    }
                    self->transport_performance_.ObserveConnected();
                    const auto query = session->get_request().get_query();
                    auto query_parameters = UrlHelper::ParseQueryString(
                        std::string(query.data(), query.size()));
                    const auto socket_fd = get_socket_fd(session);
                    if (!self->async_scope_->Spawn(
                            "ws-session-open",
                            [weak_self, session, path, socket_fd,
                             query_parameters =
                                 std::move(query_parameters)]() mutable {
                                return WsServer::OpenWebSocketAsync(
                                    weak_self, session, path,
                                    std::move(query_parameters), socket_fd);
                            })) {
                        self->transport_performance_.ObserveDropped();
                        RejectWebSocketSession(session,
                                               kWsAuthorizationRejectedSignal);
                    }
                })
            .on("close",
                [weak_self, path,
                 get_socket_fd](std::shared_ptr<asio2::http_session>& session) {
                    auto self = weak_self.lock();
                    if (!self || self->exiting_) {
                        return;
                    }
                    self->transport_performance_.ObserveDisconnected();
                    const auto socket_fd = get_socket_fd(session);
                    LOGI("client closed: {}", socket_fd);
                    if (path == kUrlMedia) {
                        if (auto removed_stream_router =
                                self->stream_routers_.Remove(socket_fd);
                            removed_stream_router.has_value()) {
                            const auto& router = removed_stream_router.value();
                            router->OnClose(session);
                            self->UpdateUdpMediaAssociation(
                                router->udp_media_association_code_,
                                router->logical_session_id_, router->stream_id_,
                                false, true);
                            self->CloseLogicalSessionBinding(
                                router->logical_session_id_,
                                router->binding_id_);
                            self->NotifyMediaClientDisConnected(
                                router->connection_id_, router->stream_id_,
                                router->visitor_device_id_,
                                router->created_timestamp_, router->binding_id_,
                                router->logical_session_id_);
                            LOGI(
                                "event=session.close component=net_ws "
                                "outcome=removed "
                                "device={}",
                                PrivacyLogId(router->visitor_device_id_));
                        }
                    } else if (path == kUrlFileTransfer) {
                        if (auto removed = self->ft_routers_.Remove(socket_fd);
                            removed.has_value()) {
                            const auto& router = removed.value();
                            self->CloseLogicalSessionBinding(
                                router->logical_session_id_,
                                router->binding_id_);
                            router->OnClose(session);
                            self->NotifyMediaClientDisConnected(
                                router->connection_id_, router->stream_id_,
                                router->device_id_, router->created_timestamp_,
                                router->binding_id_,
                                router->logical_session_id_);
                        }
                    }
                })
            .on_ping([weak_self](auto& session) {

            })
            .on_pong([weak_self](auto& session) {

            })
            .on("update", [](std::shared_ptr<asio2::http_session>& session) {
                LOGI("update");
            }));
}

void WsServer::CloseLogicalSessionBinding(const std::string& logical_session_id,
                                          const std::string& binding_id) {
    if (logical_session_id.empty() || binding_id.empty()) {
        return;
    }
    if (frontend_lease_renewals_) {
        frontend_lease_renewals_->Cancel(binding_id);
    }
    const auto event = std::make_shared<CloseLogicalSessionBindingEvent>();
    event->logical_session_id_ = logical_session_id;
    event->binding_id_ = binding_id;
    if (const auto transport = transport_.lock()) {
        transport->EmitEvent(event);
    }
}

void WsServer::UpdateUdpMediaAssociation(const std::string& association_code,
                                         const std::string& logical_session_id,
                                         const std::string& stream_id,
                                         const bool force_gdi,
                                         const bool revoke) {
    if (association_code.empty()) {
        return;
    }
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const auto transport = transport_.lock();
    const auto updated =
        transport &&
        transport->UpdateUdpAssociation(UdpMediaAssociation{
            .association_code_ = association_code,
            .logical_session_id_ = logical_session_id,
            .stream_id_ = stream_id,
            .expires_at_ms_ = now_ms + std::chrono::seconds(15).count() * 1000,
            .force_gdi_ = force_gdi,
            .revoke_ = revoke,
        });
    if (!updated) {
        LOGE(
            "event=transport.route component=net_ws "
            "code=MODULE_DEPENDENCY_UNAVAILABLE operation=udp_association "
            "outcome=failed recoverable=true action={} stream={}",
            revoke ? "revoke" : "register", PrivacyLogId(stream_id));
        return;
    }
    LOGI(
        "event=transport.route component=net_ws operation=udp_association "
        "outcome=success action={} stream={}",
        revoke ? "revoke" : "register", PrivacyLogId(stream_id));
}

void WsServer::AddHttpRouter(
    const std::string& path,
    std::function<void(const std::string& path,
                       std::shared_ptr<asio2::http_session>& session_ptr,
                       http::web_request& req, http::web_response& rep)>&&
        callback) {
    if (rdp_proxy_port_ != 0) {
        return;
    }
    auto weak_self = weak_from_this();
    // bind it
    server_->bind<http::verb::get, http::verb::post>(
        path,
        [weak_self, path, callback = std::move(callback)](
            std::shared_ptr<asio2::http_session>& session_ptr,
            http::web_request& req, http::web_response& rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            callback(path, session_ptr, req, rep);
        },
        aop_log{});  //, http::enable_cache
}

void WsServer::AddWebClientRouter() {
    if (rdp_proxy_port_ != 0) {
        return;
    }
    auto web_client_dir =
        std::filesystem::path(FolderUtil::GetCurrentFolderPath()) /
        "web_client";
    std::error_code ec;
    if (!std::filesystem::is_directory(web_client_dir, ec)) {
        LOGW(
            "event=module.start component=net_ws "
            "code=WEB_CLIENT_DIRECTORY_MISSING operation=serve_web_client "
            "outcome=disabled recoverable=true");
        return;
    }

    // make rep.fill_file() resolve paths relative to the web client dir
    server_->set_root_directory(web_client_dir);

    // serve a file under the web client dir; fallback to index.html for SPA
    // routes
    auto fn_serve = [web_client_dir](http::web_request& req,
                                     http::web_response& rep) {
        // url_path: "/web" or "/web/xxx"
        std::string url_path(req.path());
        std::string rel;
        if (url_path.size() > kUrlWebClient.size()) {
            rel = url_path.substr(kUrlWebClient.size());
            while (!rel.empty() &&
                   (rel.front() == '/' || rel.front() == '\\')) {
                rel.erase(rel.begin());
            }
        }
        std::error_code fs_ec;
        if (rel.empty() || rel.find("..") != std::string::npos ||
            !std::filesystem::is_regular_file(
                web_client_dir / std::filesystem::path(rel), fs_ec)) {
            rel = "index.html";
        }
        LOGI(
            "event=transport.http component=net_ws operation=serve_web_client "
            "outcome=success route={} asset={}",
            PrivacyLogId(url_path), PrivacyLogId(rel));
        // note: asio2 detail::make_filepath appends `path` with operator+= (no
        // separator), so the path must carry a leading '/'
        rep.fill_file(std::filesystem::path("/") / rel);
    };

    auto weak_self = weak_from_this();
    // "/web" and "/web/" (trailing slashes are stripped by the router)
    server_->bind<http::verb::get>(
        kUrlWebClient,
        [weak_self, fn_serve](std::shared_ptr<asio2::http_session>& session_ptr,
                              http::web_request& req,
                              http::web_response& rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            fn_serve(req, rep);
        },
        aop_log{});
    // "/web/xxx"
    server_->bind<http::verb::get>(
        kUrlWebClientWildcard,
        [weak_self, fn_serve](std::shared_ptr<asio2::http_session>& session_ptr,
                              http::web_request& req,
                              http::web_response& rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            fn_serve(req, rep);
        },
        aop_log{});
    LOGI(
        "event=module.start component=net_ws operation=serve_web_client "
        "outcome=success route=/web");
}

void WsServer::NotifyMediaClientConnected(
    const std::string& conn_id, const std::string& stream_id,
    const std::string& visitor_device_id, const std::string& logical_session_id) {
    auto event = std::make_shared<ClientConnectedEvent>();
    event->logical_session_id_ = logical_session_id;
    event->connection_id_ = conn_id;
    event->stream_id_ = stream_id;
    event->connection_type_ = "Direct";
    event->visitor_device_id_ = visitor_device_id;
    event->begin_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
    if (const auto transport = transport_.lock()) {
        transport->EmitEvent(event);
    }
    LOGI(
        "event=session.admit component=net_ws outcome=connected "
        "stream={} device={}",
        PrivacyLogId(stream_id), PrivacyLogId(visitor_device_id));
}

void WsServer::NotifyMediaClientDisConnected(
    const std::string& conn_id, const std::string& stream_id,
    const std::string& visitor_device_id, const int64_t begin_timestamp,
    const std::string& connection_instance_id,
    const std::string& logical_session_id) {
    auto event = std::make_shared<ClientDisconnectedEvent>();
    event->connection_id_ = conn_id;
    event->connection_instance_id_ = connection_instance_id;
    event->logical_session_id_ = logical_session_id;
    event->stream_id_ = stream_id;
    event->visitor_device_id_ = visitor_device_id;
    event->end_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
    event->duration_ = event->end_timestamp_ - begin_timestamp;
    if (const auto transport = transport_.lock()) {
        transport->EmitEvent(event);
    }
}

int64_t WsServer::GetQueuingMediaMsgCount() {
    int64_t count = 0;
    stream_routers_.ApplyAll([&](const auto&, const auto& router) {
        count += router->GetQueuingMsgCount();
    });
    return count;
}

int64_t WsServer::GetQueuingFtMsgCount() {
    int64_t count = 0;
    ft_routers_.ApplyAll([&](const auto&, const auto& router) {
        count += router->GetQueuingMsgCount();
    });
    return count;
}

std::vector<std::shared_ptr<PxConnectedClientInfo>>
WsServer::GetConnectedClientInfo() {
    std::vector<std::shared_ptr<PxConnectedClientInfo>> clients_info;
    stream_routers_.ApplyAll(
        [&](const auto&, const std::shared_ptr<WsStreamRouter>& router) {
            std::string device_name;
            {
                std::lock_guard<std::mutex> lock(router->device_name_mtx_);
                device_name = router->device_name_;
            }
            clients_info.push_back(
                std::make_shared<PxConnectedClientInfo>(PxConnectedClientInfo{
                    .device_id_ = router->visitor_device_id_,
                    .stream_id_ = router->stream_id_,
                    .device_name_ = device_name,
                }));
        });
    return clients_info;
}

void WsServer::OnClientHello(const std::shared_ptr<MsgClientHello>& event) {
    stream_routers_.ApplyAll(
        [&](const auto&, const std::shared_ptr<WsStreamRouter>& router) {
            LOGI(
                "event=session.hello component=net_ws outcome=received "
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
                if (router->udp_media_.load() &&
                    !router->udp_media_association_code_.empty()) {
                    UpdateUdpMediaAssociation(
                        router->udp_media_association_code_,
                        router->logical_session_id_, router->stream_id_,
                        router->force_gdi_, false);
                }
            }
        });
}
}  // namespace px
