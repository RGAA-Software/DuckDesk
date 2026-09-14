//
// Created by RGAA on 2023/12/20.
//
#include "http_handler.h"
#include "version_config.h"
#include "px_common/log.h"
#include "px_common/privacy_log.h"
#include "px_common/md5.h"
#include "px_common/data.h"
#include "px_common/uuid.h"
#include "ws_transport.h"
#include "ws_callback_workflow.h"
#include "px_render/network/transport_types.h"
#include "px_render/architecture/events/render_event.h"
#include <algorithm>
#include <chrono>
#include <vector>
#include "px_common/async_operation.h"
#include "px_common/async_result.h"
#include "px_render/architecture/runtime/await_callback.h"

namespace px {

constexpr auto kHandlerErrVerifySafetyPasswordFailed = 700;
constexpr auto kHandlerErrNoSafetyPasswordInRenderer = 701;
constexpr auto kHandlerErrNoWebRtcLocalLibrary = 702;
constexpr auto kHandlerErrCreateRtcLocalServerFailed = 703;
constexpr auto kHandlerErrRtcLocalOccupied = 704;
constexpr auto kHandlerErrSessionRejected = 705;
constexpr auto kHandlerErrRemoteAccessDisabled = 706;
constexpr auto kHandlerErrIpDirectAuthorizationRejected = 707;

int64_t CurrentSystemMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct RtcPasswordAdmission {
    std::vector<std::string> permissions_;
    std::string logical_session_id_;
    std::string stream_id_;
    std::string join_mode_;
    std::string subject_id_;
    int64_t expires_at_ms_ = 0;
    bool allow_observer_ = true;
    bool allow_takeover_ = true;
};

struct DeferredHttpReply {
    std::string payload_;
    http::status status_ = http::status::ok;
};

HttpHandler::HttpHandler(std::weak_ptr<WsTransport> transport, std::shared_ptr<PxAsyncScope> async_scope)
    : transport_(std::move(transport)), async_scope_(std::move(async_scope)) {}

std::string HttpHandler::GetErrorMessage(int code) {
    if (code == kHandlerErrVerifySafetyPasswordFailed) {
        return "Verify security password failed";
    } else if (code == kHandlerErrNoSafetyPasswordInRenderer) {
        return "No security password in renderer";
    } else if (code == kHandlerErrNoWebRtcLocalLibrary) {
        return "No WebRTC local network library";
    } else if (code == kHandlerErrCreateRtcLocalServerFailed) {
        return "Create Rtc local server failed";
    } else if (code == kHandlerErrRtcLocalOccupied) {
        return "Rtc local connection occupied";
    } else if (code == kHandlerErrSessionRejected) {
        return "Session rejected";
    } else if (code == kHandlerErrRemoteAccessDisabled) {
        return "Remote access is disabled on the remote device";
    } else if (code == kHandlerErrIpDirectAuthorizationRejected) {
        return "IP direct authorization rejected";
    }
    return BaseHandler::GetErrorMessage(code);
}

void HttpHandler::HandlePing(http::web_request& req, http::web_response& resp) {
    auto data = WrapBasicInfo(200, "ok", std::string("Pong"));
    resp.fill_json(data);
}

void HttpHandler::HandleVerifySecurityPassword(http::web_request& req, http::web_response& resp) {
    auto params = GetQueryParams(req.query());
    auto value = GetParam(params, "safety_pwd_md5");
    if (!value.has_value()) {
        SendErrorJson(resp, kHandlerErrParams);
        return;
    }

    // same rules as /alloc/local/rtc: accept safety pwd(md5) or random pwd(plain/md5),
    // and pass when the device has no password at all
    if (!VerifySafetyPassword(params)) {
        SendErrorJson(resp, kHandlerErrVerifySafetyPasswordFailed);
        return;
    }
    SendOkJson(resp, "");
}

void HttpHandler::HandleGetRenderConfiguration(http::web_request& req, http::web_response& resp) {
    const auto transport = transport_.lock();
    if (!transport) {
        SendErrorJson(resp, kHandlerErrParams);
        return;
    }
    const auto& settings = transport->Settings();
    nlohmann::json obj;
    obj["device_id"] = settings.device_id;
    obj["relay_host"] = settings.relay_host;
    obj["relay_port"] = std::atoi(settings.relay_port.c_str());
    obj["incoming_remote_access_enabled"] = settings.incoming_remote_access_enabled;
    obj["file_transfer_enabled"] = settings.file_transfer_enabled;
    const auto controller = transport->QueryControllerAvailability(CurrentSystemMilliseconds());
    obj["controller_availability_known"] = controller.known;
    obj["controller_available"] = controller.available;
    obj["controller_reconnect_grace"] = controller.reconnect_grace;
    obj["controller_retry_after_ms"] = controller.retry_after_ms;
    // Web 端鼠标回放需要当前采集显示器名(event_replayer 按它定位坐标系)
    obj["monitor_name"] = transport->CapturingMonitorName();
    // 供 Web 客户端展示,便于确认被控端是否为旧版本
    obj["app_version"] = PROJECT_VERSION;
    SendOkJson(resp, obj.dump());
}

void HttpHandler::HandlePanelStreamMessage(http::web_request& req, http::web_response& resp) {
    const auto transport = transport_.lock();
    if (!transport) {
        SendErrorJson(resp, kHandlerErrParams);
        return;
    }
    auto& body = req.body();
    auto target = req.target();
    if (body.empty()) {
        SendErrorJson(resp, kHandlerErrBody);
        return;
    }

    auto event = std::make_shared<PanelStreamMessageEvent>();
    event->body_ = Data::From(body);
    transport->EmitEvent(event);

    SendOkJson(resp, "");
}

bool HttpHandler::VerifySafetyPassword(const std::unordered_map<std::string, std::string>& params) {
    const auto transport = transport_.lock();
    if (!transport) {
        return false;
    }
    auto settings = transport->Settings();
    if (settings.device_safety_password.empty() && settings.device_random_password.empty()) {
        return true;
    }
    auto value = GetParam(params, "safety_pwd_md5");
    if (!value.has_value() || value.value().empty()) {
        return false;
    }
    // 安全密码:存的就是 MD5,直接比对
    if (!settings.device_safety_password.empty() && settings.device_safety_password == value.value()) {
        return true;
    }
    // 临时(随机)密码:存的是明文,兼容"前端 md5 后传入"和"直接传明文"两种形式
    if (!settings.device_random_password.empty()) {
        if (settings.device_random_password == value.value()) {
            return true;
        }
        if (MD5::Hex(settings.device_random_password) == value.value()) {
            return true;
        }
    }
    return false;
}

void HttpHandler::CloseAdmittedLogicalSessionBinding(const std::string& logical_session_id, const std::string& binding_id) {
    if (logical_session_id.empty() || binding_id.empty()) {
        return;
    }
    const auto event = std::make_shared<CloseLogicalSessionBindingEvent>();
    event->logical_session_id_ = logical_session_id;
    event->binding_id_ = binding_id;
    event->preserve_reconnect_grace_ = false;
    if (const auto transport = transport_.lock()) {
        transport->EmitEvent(event);
    }
}

void HttpHandler::HandleAllocLocalRtc(std::shared_ptr<asio2::http_session>& session_ptr, http::web_request& req, http::web_response& resp) {
    if (transport_.expired() || !async_scope_ || !async_scope_->IsAccepting()) {
        SendErrorJson(resp, kHandlerErrNoWebRtcLocalLibrary);
        return;
    }
    LOGI("event=workflow.start component=net_ws operation=rtc_local_allocate "
         "outcome=accepted peer={} remote_port={} local_port={}",
         PrivacyLogId(session_ptr->remote_address()), session_ptr->remote_port(), session_ptr->local_port());
    auto params = GetQueryParams(req.query());
    auto body = std::string(req.body());
    auto remote_address = std::string(session_ptr->remote_address());
    auto response_defer = resp.defer();
    const auto weak_self = weak_from_this();
    const auto session = session_ptr;
    if (!async_scope_->Spawn("rtc-local-http-allocation",
                             [weak_self, session, params = std::move(params), body = std::move(body), remote_address = std::move(remote_address),
                              response_defer = std::move(response_defer)]() mutable {
                                 return AllocateLocalRtcAsync(weak_self, session, std::move(params), std::move(body), std::move(remote_address),
                                                              std::move(response_defer));
                             })) {
        SendErrorJson(resp, kHandlerErrCreateRtcLocalServerFailed);
    }
}

PxAwaitable<void> HttpHandler::AllocateLocalRtcAsync(std::weak_ptr<HttpHandler> owner, std::shared_ptr<asio2::http_session> session,
                                                     std::unordered_map<std::string, std::string> params, std::string body,
                                                     std::string remote_address, std::shared_ptr<http::response_defer> response_defer) {
    const auto self = owner.lock();
    const auto transport = self ? self->transport_.lock() : nullptr;
    if (!self || !transport) {
        session->post_queued_event([session, response_defer = std::move(response_defer)]() mutable {
            session->response().fill_json(R"({"code":702,"message":"No WebRTC local network library","data":""})");
            response_defer.reset();
        });
        co_return;
    }
    const auto make_reply = [self](const int code, const http::status status = http::status::ok, const std::string& data = std::string{}) {
        return DeferredHttpReply{
            .payload_ = self->WrapBasicInfo(code, self->GetErrorMessage(code), data),
            .status_ = status,
        };
    };
    const auto complete = [session, response_defer](DeferredHttpReply reply) {
        session->post_queued_event([session, response_defer, reply = std::move(reply)]() mutable {
            session->response().fill_json(reply.payload_, reply.status_);
            response_defer.reset();
        });
    };

    std::string sdp;
    std::string body_nonce;
    try {
        const auto object = nlohmann::json::parse(body);
        sdp = object.at("sdp").get<std::string>();
        body_nonce = object.value("client_nonce", "");
    } catch (...) {
        complete(make_reply(kHandlerErrParams));
        co_return;
    }
    const auto device_id = self->GetParam(params, "device_id").value_or(std::string{});
    if (sdp.empty()) {
        complete(make_reply(kHandlerErrParams));
        co_return;
    }
    const auto nonce_param = self->GetParam(params, "client_nonce");
    const auto client_nonce = !body_nonce.empty() ? body_nonce : nonce_param.value_or(std::string{});
    if (client_nonce.empty()) {
        complete(make_reply(kHandlerErrParams));
        co_return;
    }
    if (!self->VerifySafetyPassword(params)) {
        LOGW("event=session.admit component=net_ws code=SESSION_PASSWORD_REJECTED operation=rtc_password_auth "
             "outcome=rejected recoverable=false device={}",
             PrivacyLogId(device_id));
        complete(make_reply(kHandlerErrVerifySafetyPasswordFailed, http::status::forbidden));
        co_return;
    }
    const auto requested_stream_id = self->GetParam(params, "stream_id").value_or(std::string{});
    const auto stream_id =
        requested_stream_id.empty() ? std::string("password:") + MD5::Hex(remote_address + "|" + client_nonce) : requested_stream_id;
    RtcPasswordAdmission authentication{
        .permissions_ = {"view", "input", "clipboard", "file", "audio"},
        .logical_session_id_ = std::string("password:") + MD5::Hex(device_id + "|" + stream_id + "|" + client_nonce),
        .stream_id_ = stream_id,
        .join_mode_ = "control",
        .subject_id_ = std::string("password:") + MD5::Hex(remote_address + "|" + client_nonce),
        .expires_at_ms_ = 0,
        .allow_observer_ = false,
        .allow_takeover_ = transport->Settings().direct_allow_takeover,
    };

    if (!transport->HasLocalRtcService()) {
        complete(make_reply(kHandlerErrNoWebRtcLocalLibrary));
        co_return;
    }
    const bool takeover_requested = [&] {
        const auto value = self->GetParam(params, "takeover");
        return value && (*value == "1" || *value == "true");
    }();
    const auto admitted_binding_id = std::string("rtc-local:") + authentication.stream_id_;
    const auto admission_grant = LogicalSessionGrant{
        .logical_session_id = authentication.logical_session_id_,
        .stream_id = authentication.stream_id_,
        .subject_id = authentication.subject_id_,
        .join_mode = authentication.join_mode_,
        .expires_at_ms = authentication.expires_at_ms_,
        .allow_observer = authentication.allow_observer_,
        .allow_takeover = authentication.allow_takeover_,
        .input_allowed = true,
    };
    auto admitted = co_await AwaitWsValueCallback<LogicalSessionAdmission>(
        [weak_transport = self->transport_, admission_grant, admitted_binding_id,
         takeover_requested](std::function<void(LogicalSessionAdmission)> completion) {
            const auto active_plugin = weak_transport.lock();
            if (!active_plugin) {
                return false;
            }
            const auto event = std::make_shared<AdmitLogicalSessionEvent>();
            event->grant_ = admission_grant;
            event->transport_ = LogicalSessionTransport::kRtcLocal;
            event->binding_id_ = admitted_binding_id;
            event->takeover_ = takeover_requested;
            event->callback_ = std::move(completion);
            active_plugin->EmitEvent(event);
            return true;
        },
        std::chrono::steady_clock::now() + std::chrono::seconds(3), "rtc_session_admit",
        [owner, logical_session_id = authentication.logical_session_id_, admitted_binding_id](const LogicalSessionAdmission& late) {
            if (late.code == LogicalSessionAdmissionCode::kAccepted) {
                if (const auto active_owner = owner.lock()) {
                    active_owner->CloseAdmittedLogicalSessionBinding(logical_session_id, admitted_binding_id);
                }
            }
        });
    if (!admitted.HasValue()) {
        complete(make_reply(kHandlerErrSessionRejected));
        co_return;
    }
    const auto admission = admitted.TakeValue();
    if (admission.code != LogicalSessionAdmissionCode::kAccepted) {
        const auto code = admission.code == LogicalSessionAdmissionCode::kRemoteAccessDisabled
                              ? kHandlerErrRemoteAccessDisabled
                              : (admission.code == LogicalSessionAdmissionCode::kOccupied ? kHandlerErrRtcLocalOccupied : kHandlerErrSessionRejected);
        complete(make_reply(code, http::status::forbidden));
        co_return;
    }

    const auto rtc_request = std::make_shared<PxLocalRtcRequestInfo>();
    rtc_request->device_id_ = device_id;
    rtc_request->stream_id_ = authentication.stream_id_;
    rtc_request->req_ip_ = remote_address;
    rtc_request->sdp_ = sdp;
    rtc_request->content_type_ = self->GetParam(params, "content_type") == std::optional<std::string>("game_stream")
                                     ? PxLocalRtcContentType::kGameStream
                                     : PxLocalRtcContentType::kDesktop;
    rtc_request->capability_enforced_ = true;
    rtc_request->takeover_ = takeover_requested;
    if (admission.role == LogicalSessionRole::kObserver) {
        rtc_request->session_role_ = PxLocalRtcSessionRole::kObserver;
        rtc_request->permissions_ = {"view", "audio"};
    } else {
        rtc_request->permissions_ = {"view", "input", "clipboard", "file", "audio"};
    }
    if (self->GetParam(params, "session_role") == std::optional<std::string>("wall_observer")) {
        if (remote_address != "127.0.0.1" && remote_address != "::1") {
            self->CloseAdmittedLogicalSessionBinding(authentication.logical_session_id_, admitted_binding_id);
            complete(make_reply(kHandlerErrParams));
            co_return;
        }
        rtc_request->session_role_ = PxLocalRtcSessionRole::kWallObserver;
    }
    rtc_request->client_nonce_ = !body_nonce.empty() ? body_nonce : self->GetParam(params, "client_nonce").value_or(std::string{});

    const auto executor = co_await asio::this_coro::executor;
    const auto rtc_operation = PxAsyncOneShot<std::shared_ptr<PxLocalRtcReplyInfo>>::Create(executor);
    const std::weak_ptr<PxAsyncOneShot<std::shared_ptr<PxLocalRtcReplyInfo>>> weak_rtc_operation = rtc_operation;
    const auto allocation = transport->AllocateLocalRtcInstance(rtc_request, [weak_rtc_operation](const std::shared_ptr<PxLocalRtcReplyInfo>& reply) {
        if (const auto operation = weak_rtc_operation.lock()) {
            if (reply) {
                static_cast<void>(operation->TryComplete(PxResult<std::shared_ptr<PxLocalRtcReplyInfo>>::Success(reply)));
            } else {
                static_cast<void>(operation->TryFail(
                    MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "rtc_local_allocate", "RTC reply is empty", false, "RTC_LOCAL_EMPTY_REPLY")));
            }
        }
    });
    if (allocation != PxLocalRtcAllocResult::kOk) {
        static_cast<void>(rtc_operation->TryFail(
            MakePxAsyncError(PxAsyncErrorCode::kServiceRejected, "rtc_local_allocate", "RTC allocation was rejected", false,
                             allocation == PxLocalRtcAllocResult::kOccupied ? "RTC_LOCAL_OCCUPIED" : "RTC_LOCAL_CREATE_FAILED")));
    }
    auto rtc_reply = co_await PxAsyncOneShot<std::shared_ptr<PxLocalRtcReplyInfo>>::WaitUntil(rtc_operation, std::chrono::steady_clock::now() +
                                                                                                                 std::chrono::seconds(10));
    if (!rtc_reply.HasValue()) {
        self->CloseAdmittedLogicalSessionBinding(authentication.logical_session_id_, admitted_binding_id);
        const auto code = rtc_reply.Error().detail_code == "RTC_LOCAL_OCCUPIED" ? kHandlerErrRtcLocalOccupied : kHandlerErrCreateRtcLocalServerFailed;
        LOGW("event=workflow.complete component=net_ws code={} "
             "operation=rtc_local_allocate outcome=failed recoverable={} "
             "reason={}",
             rtc_reply.Error().StableCode(), rtc_reply.Error().retryable, rtc_reply.Error().message);
        complete(make_reply(code));
        co_return;
    }

    const auto reply_info = rtc_reply.TakeValue();
    nlohmann::json result;
    result["answer_sdp"] = reply_info->answer_sdp_;
    auto monitors = nlohmann::json::array();
    int monitor_index = 0;
    for (const auto& monitor : reply_info->monitors_) {
        monitors.push_back({
            {"name", monitor.name_},
            {"width", monitor.width_},
            {"height", monitor.height_},
            {"left", monitor.left_},
            {"top", monitor.top_},
            {"right", monitor.right_},
            {"bottom", monitor.bottom_},
            {"index", monitor_index++},
        });
    }
    result["monitors"] = monitors;
    result["stream_id"] = authentication.stream_id_;
    LOGI("event=session.admit component=net_ws operation=rtc_password_auth outcome=accepted device={} takeover={}", PrivacyLogId(device_id),
         rtc_request->takeover_);
    complete(DeferredHttpReply{
        .payload_ = self->WrapBasicInfo(200, self->GetErrorMessage(200), result),
        .status_ = http::status::ok,
    });
    co_return;
}
} // namespace px
