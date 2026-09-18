//
// Created by RGAA on 2023/12/20.
//
#include "http_handler.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "px_common/async_delay.h"
#include "px_common/async_operation.h"
#include "px_common/async_result.h"
#include "px_common/data.h"
#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_common/privacy_log.h"
#include "px_common/uuid.h"
#include "px_render/architecture/events/render_event.h"
#include "px_render/architecture/runtime/await_callback.h"
#include "px_render/network/transport_types.h"
#include "version_config.h"
#include "ws_callback_workflow.h"
#include "ws_transport.h"

namespace px {

constexpr auto kHandlerErrVerifySafetyPasswordFailed = 700;
constexpr auto kHandlerErrNoSafetyPasswordInRenderer = 701;
constexpr auto kHandlerErrNoWebRtcLocalLibrary = 702;
constexpr auto kHandlerErrCreateRtcLocalServerFailed = 703;
constexpr auto kHandlerErrRtcLocalOccupied = 704;
constexpr auto kHandlerErrSessionRejected = 705;
constexpr auto kHandlerErrRemoteAccessDisabled = 706;
constexpr auto kHandlerErrIpDirectAuthorizationRejected = 707;
constexpr auto kHandlerErrConsoleAdmissionRejected = 708;

int64_t CurrentSystemMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
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

class SecureFrontendToken final {
public:
    explicit SecureFrontendToken(std::string value) : value_(std::move(value)) {}

    ~SecureFrontendToken() { std::fill(value_.begin(), value_.end(), '\0'); }

    SecureFrontendToken(const SecureFrontendToken&) = delete;
    SecureFrontendToken& operator=(const SecureFrontendToken&) = delete;

    [[nodiscard]] std::string Copy() const { return value_; }

private:
    std::string value_;
};

struct FrontendLeaseIdentity final {
    ConsoleFrontendGrant expected_grant;
    LogicalSessionGrant logical_grant;
    std::string descriptor_session_id;
    std::int64_t descriptor_revision{0};
    std::string device_id;
    std::string stream_id;
    std::string allocation_id;
    std::string binding_id;
};

struct FrontendLeaseControl final {
    std::atomic_bool current{true};
};

[[nodiscard]] bool MatchesExpectedGrant(const ConsoleFrontendGrant& expected, const ConsoleFrontendGrant& renewed) {
    return renewed.valid_for_ms > 0 && renewed.session_id == expected.session_id && renewed.revision == expected.revision &&
           renewed.target_kind == expected.target_kind && renewed.device_id == expected.device_id &&
           renewed.application_id == expected.application_id && renewed.instance_id == expected.instance_id &&
           renewed.client_type == expected.client_type && renewed.access_role == expected.access_role;
}

[[nodiscard]] std::chrono::milliseconds RenewalDelay(const std::uint32_t valid_for_ms) {
    const auto third = std::chrono::milliseconds(valid_for_ms / 3);
    return std::clamp(third, std::chrono::milliseconds(1000), std::chrono::milliseconds(10000));
}

class FrontendLeaseRenewalCoordinator final : public std::enable_shared_from_this<FrontendLeaseRenewalCoordinator> {
public:
    FrontendLeaseRenewalCoordinator(std::weak_ptr<WsTransport> transport, std::shared_ptr<PxAsyncScope> async_scope)
        : transport_(std::move(transport)), async_scope_(std::move(async_scope)) {}

    void Start(FrontendLeaseIdentity identity, std::shared_ptr<SecureFrontendToken> token, const std::uint32_t initial_valid_for_ms) {
        if (!token || !async_scope_ || !async_scope_->IsAccepting() || initial_valid_for_ms == 0) {
            return;
        }
        const auto control = std::make_shared<FrontendLeaseControl>();
        {
            std::scoped_lock lock(controls_mutex_);
            const auto existing = controls_.find(identity.binding_id);
            if (existing != controls_.end()) {
                existing->second->current.store(false, std::memory_order_release);
            }
            controls_.insert_or_assign(identity.binding_id, control);
        }
        const auto weak_owner = weak_from_this();
        const bool spawned = async_scope_->Spawn("console-frontend-lease-renewal", [weak_owner, control, identity = std::move(identity),
                                                                                    token = std::move(token), initial_valid_for_ms]() mutable {
            return Run(std::move(weak_owner), std::move(control), std::move(identity), std::move(token), initial_valid_for_ms);
        });
        if (!spawned) {
            control->current.store(false, std::memory_order_release);
            RemoveCurrent(control);
        }
    }

private:
    static PxAwaitable<void> Run(std::weak_ptr<FrontendLeaseRenewalCoordinator> weak_owner, std::shared_ptr<FrontendLeaseControl> control,
                                 FrontendLeaseIdentity identity, std::shared_ptr<SecureFrontendToken> token, std::uint32_t valid_for_ms) {
        auto lease_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(valid_for_ms);
        auto delay = RenewalDelay(valid_for_ms);
        for (;;) {
            const auto waited = co_await WaitForAsyncDelay(delay, "frontend_lease_delay");
            const auto owner = weak_owner.lock();
            if (!waited || !owner || !control->current.load(std::memory_order_acquire)) {
                co_return;
            }
            const auto transport = owner->transport_.lock();
            if (!transport) {
                co_return;
            }
            const auto request_started_at = std::chrono::steady_clock::now();
            const auto request_deadline = std::min(request_started_at + std::chrono::seconds(12), lease_deadline);
            auto renewed = co_await transport->AdmitFrontend(
                ConsoleFrontendAdmissionRequest{
                    .request_id = GetUUID(),
                    .session_id = identity.descriptor_session_id,
                    .revision = identity.descriptor_revision,
                    .frontend_token = token->Copy(),
                },
                request_deadline);
            if (!control->current.load(std::memory_order_acquire)) {
                co_return;
            }
            if (!renewed.HasValue()) {
                const auto now = std::chrono::steady_clock::now();
                if (renewed.Error().retryable && now < lease_deadline) {
                    delay = std::min(std::chrono::milliseconds(2000), std::chrono::duration_cast<std::chrono::milliseconds>(lease_deadline - now));
                    continue;
                }
                owner->TerminateCurrent(identity, control, renewed.Error().StableCode());
                co_return;
            }
            const auto grant = renewed.TakeValue();
            if (!MatchesExpectedGrant(identity.expected_grant, grant)) {
                owner->TerminateCurrent(identity, control, "FRONTEND_LEASE_IDENTITY_CHANGED");
                co_return;
            }
            auto renewed_logical_grant = identity.logical_grant;
            renewed_logical_grant.expires_at_ms = CurrentSystemMilliseconds() + static_cast<std::int64_t>(grant.valid_for_ms);
            if (!transport->RenewLogicalSessionLease(renewed_logical_grant, CurrentSystemMilliseconds())) {
                owner->TerminateCurrent(identity, control, "LOGICAL_LEASE_RENEWAL_REJECTED");
                co_return;
            }
            lease_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(grant.valid_for_ms);
            delay = RenewalDelay(grant.valid_for_ms);
        }
    }

    void TerminateCurrent(const FrontendLeaseIdentity& identity, const std::shared_ptr<FrontendLeaseControl>& control, const std::string& reason) {
        if (!control->current.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        if (const auto transport = transport_.lock()) {
            static_cast<void>(transport->RevokeLocalRtcInstance(identity.device_id, identity.stream_id, identity.allocation_id));
            const auto close = std::make_shared<CloseLogicalSessionBindingEvent>();
            close->logical_session_id_ = identity.logical_grant.logical_session_id;
            close->binding_id_ = identity.binding_id;
            close->preserve_reconnect_grace_ = false;
            transport->EmitEvent(close);
        }
        LOGW("event=session.lease component=net_ws operation=renew outcome=revoked session={} reason={}",
             PrivacyLogId(identity.logical_grant.logical_session_id), reason);
        RemoveCurrent(control);
    }

    void RemoveCurrent(const std::shared_ptr<FrontendLeaseControl>& expected_control) {
        std::scoped_lock lock(controls_mutex_);
        for (auto iterator = controls_.begin(); iterator != controls_.end();) {
            if (iterator->second == expected_control) {
                iterator = controls_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    std::weak_ptr<WsTransport> transport_;
    std::shared_ptr<PxAsyncScope> async_scope_;
    std::mutex controls_mutex_;
    std::unordered_map<std::string, std::shared_ptr<FrontendLeaseControl>> controls_;
};

HttpHandler::HttpHandler(std::weak_ptr<WsTransport> transport, std::shared_ptr<PxAsyncScope> async_scope)
    : transport_(std::move(transport)),
      async_scope_(std::move(async_scope)),
      frontend_lease_renewals_(std::make_shared<FrontendLeaseRenewalCoordinator>(transport_, async_scope_)) {}

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
    } else if (code == kHandlerErrConsoleAdmissionRejected) {
        return "Console frontend admission rejected";
    }
    return BaseHandler::GetErrorMessage(code);
}

void HttpHandler::HandlePing(http::web_request& req, http::web_response& resp) {
    auto data = WrapBasicInfo(200, "ok", std::string("Pong"));
    resp.fill_json(data);
}

void HttpHandler::HandleVerifySecurityPassword(http::web_request& req,
                                               http::web_response& resp) {
    auto params = GetQueryParams(req.query());
    auto value = GetParam(params, "safety_pwd_md5");
    if (!value.has_value()) {
        SendErrorJson(resp, kHandlerErrParams);
        return;
    }

    // same rules as /alloc/local/rtc: accept safety pwd(md5) or random
    // pwd(plain/md5), and pass when the device has no password at all
    if (!VerifySafetyPassword(params)) {
        SendErrorJson(resp, kHandlerErrVerifySafetyPasswordFailed);
        return;
    }
    SendOkJson(resp, "");
}

void HttpHandler::HandleGetRenderConfiguration(http::web_request& req,
                                               http::web_response& resp) {
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
    obj["incoming_remote_access_enabled"] =
        settings.incoming_remote_access_enabled;
    obj["file_transfer_enabled"] = settings.file_transfer_enabled;
    const auto controller =
        transport->QueryControllerAvailability(CurrentSystemMilliseconds());
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

void HttpHandler::HandlePanelStreamMessage(http::web_request& req,
                                           http::web_response& resp) {
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

bool HttpHandler::VerifySafetyPassword(
    const std::unordered_map<std::string, std::string>& params) {
    const auto transport = transport_.lock();
    if (!transport) {
        return false;
    }
    auto settings = transport->Settings();
    if (settings.device_safety_password.empty() &&
        settings.device_random_password.empty()) {
        return true;
    }
    auto value = GetParam(params, "safety_pwd_md5");
    if (!value.has_value() || value.value().empty()) {
        return false;
    }
    // 安全密码:存的就是 MD5,直接比对
    if (!settings.device_safety_password.empty() &&
        settings.device_safety_password == value.value()) {
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

void HttpHandler::CloseAdmittedLogicalSessionBinding(
    const std::string& logical_session_id, const std::string& binding_id) {
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

void HttpHandler::HandleAllocLocalRtc(
    std::shared_ptr<asio2::http_session>& session_ptr, http::web_request& req,
    http::web_response& resp) {
    if (transport_.expired() || !async_scope_ || !async_scope_->IsAccepting()) {
        SendErrorJson(resp, kHandlerErrNoWebRtcLocalLibrary);
        return;
    }
    LOGI(
        "event=workflow.start component=net_ws operation=rtc_local_allocate "
        "outcome=accepted peer={} remote_port={} local_port={}",
        PrivacyLogId(session_ptr->remote_address()), session_ptr->remote_port(),
        session_ptr->local_port());
    auto params = GetQueryParams(req.query());
    auto body = std::string(req.body());
    auto remote_address = std::string(session_ptr->remote_address());
    auto response_defer = resp.defer();
    const auto weak_self = weak_from_this();
    const auto session = session_ptr;
    if (!async_scope_->Spawn(
            "rtc-local-http-allocation",
            [weak_self, session, params = std::move(params),
             body = std::move(body), remote_address = std::move(remote_address),
             response_defer = std::move(response_defer)]() mutable {
                return AllocateLocalRtcAsync(
                    weak_self, session, std::move(params), std::move(body),
                    std::move(remote_address), std::move(response_defer));
            })) {
        SendErrorJson(resp, kHandlerErrCreateRtcLocalServerFailed);
    }
}

PxAwaitable<void> HttpHandler::AllocateLocalRtcAsync(
    std::weak_ptr<HttpHandler> owner,
    std::shared_ptr<asio2::http_session> session,
    std::unordered_map<std::string, std::string> params, std::string body,
    std::string remote_address,
    std::shared_ptr<http::response_defer> response_defer) {
    const auto self = owner.lock();
    const auto transport = self ? self->transport_.lock() : nullptr;
    if (!self || !transport) {
        session->post_queued_event(
            [session, response_defer = std::move(response_defer)]() mutable {
                session->response().fill_json(
                    R"({"code":702,"message":)"
                    R"("No WebRTC local network library","data":""})");
                response_defer.reset();
            });
        co_return;
    }
    const auto make_reply =
        [self](const int code, const http::status status = http::status::ok,
               const std::string& response_data = std::string{}) {
            return DeferredHttpReply{
                .payload_ = self->WrapBasicInfo(
                    code, self->GetErrorMessage(code), response_data),
                .status_ = status,
            };
        };
    const auto complete = [session, response_defer](DeferredHttpReply reply) {
        session->post_queued_event(
            [session, response_defer, reply = std::move(reply)]() mutable {
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
    const auto device_id =
        self->GetParam(params, "device_id").value_or(std::string{});
    if (sdp.empty()) {
        complete(make_reply(kHandlerErrParams));
        co_return;
    }
    const auto nonce_param = self->GetParam(params, "client_nonce");
    const auto client_nonce =
        !body_nonce.empty() ? body_nonce : nonce_param.value_or(std::string{});
    if (client_nonce.empty()) {
        complete(make_reply(kHandlerErrParams));
        co_return;
    }
    const auto requested_stream_id =
        self->GetParam(params, "stream_id").value_or(std::string{});
    RtcPasswordAdmission authentication;
    std::shared_ptr<SecureFrontendToken> frontend_token;
    std::optional<ConsoleFrontendGrant> console_frontend_grant;
    std::string descriptor_session_id;
    std::int64_t descriptor_revision{0};
    if (transport->RequiresConsoleFrontendAdmission()) {
        const auto session_id =
            self->GetParam(params, "session_id").value_or(std::string{});
        const auto revision_text =
            self->GetParam(params, "session_revision").value_or(std::string{});
        auto token_entry = params.find("frontend_token");
        std::int64_t revision = 0;
        const auto parsed_revision = std::from_chars(
            revision_text.data(), revision_text.data() + revision_text.size(),
            revision);
        if (session_id.empty() || token_entry == params.end() ||
            token_entry->second.empty() || revision <= 0 ||
            parsed_revision.ec != std::errc{} ||
            parsed_revision.ptr !=
                revision_text.data() + revision_text.size()) {
            complete(make_reply(kHandlerErrParams, http::status::bad_request));
            co_return;
        }
        frontend_token = std::make_shared<SecureFrontendToken>(std::move(token_entry->second));
        params.erase(token_entry);
        auto admitted = co_await transport->AdmitFrontend(
            ConsoleFrontendAdmissionRequest{
                .request_id = GetUUID(),
                .session_id = session_id,
                .revision = revision,
                .frontend_token = frontend_token->Copy(),
            },
            std::chrono::steady_clock::now() + std::chrono::seconds(12));
        if (!admitted.HasValue()) {
            LOGW(
                "event=session.admit component=net_ws "
                "code=CONSOLE_FRONTEND_REJECTED "
                "operation=console_frontend_auth "
                "outcome=rejected recoverable={} device={} reason={}",
                admitted.Error().retryable, PrivacyLogId(device_id),
                admitted.Error().StableCode());
            complete(make_reply(kHandlerErrConsoleAdmissionRejected,
                                http::status::forbidden));
            co_return;
        }
        auto grant = admitted.TakeValue();
        const auto settings = transport->Settings();
        if (grant.target_kind != "cloud_application" ||
            grant.instance_id != settings.device_id ||
            (grant.access_role != "controller" &&
             grant.access_role != "observer") ||
            grant.valid_for_ms == 0) {
            complete(make_reply(kHandlerErrConsoleAdmissionRejected,
                                http::status::forbidden));
            co_return;
        }
        descriptor_session_id = session_id;
        descriptor_revision = revision;
        console_frontend_grant = grant;
        const auto stream_id = requested_stream_id.empty()
                                   ? grant.session_id
                                   : requested_stream_id;
        const bool controller = grant.access_role == "controller";
        authentication = RtcPasswordAdmission{
            .permissions_ =
                controller
                    ? std::vector<std::string>{"view", "input", "clipboard",
                                               "file", "audio"}
                    : std::vector<std::string>{"view", "audio"},
            .logical_session_id_ = grant.session_id,
            .stream_id_ = stream_id,
            .join_mode_ = controller ? "control" : "observe",
            .subject_id_ = grant.client_type + ":" + grant.session_id,
            .expires_at_ms_ = CurrentSystemMilliseconds() +
                              static_cast<std::int64_t>(grant.valid_for_ms),
            .allow_observer_ = !controller,
            .allow_takeover_ = false,
        };
    } else {
        if (!self->VerifySafetyPassword(params)) {
            LOGW(
                "event=session.admit component=net_ws "
                "code=SESSION_PASSWORD_REJECTED operation=rtc_password_auth "
                "outcome=rejected recoverable=false device={}",
                PrivacyLogId(device_id));
            complete(make_reply(kHandlerErrVerifySafetyPasswordFailed,
                                http::status::forbidden));
            co_return;
        }
        const auto stream_id =
            requested_stream_id.empty()
                ? std::string("password:") +
                      MD5::Hex(remote_address + "|" + client_nonce)
                : requested_stream_id;
        authentication = RtcPasswordAdmission{
            .permissions_ = {"view", "input", "clipboard", "file", "audio"},
            .logical_session_id_ =
                std::string("password:") +
                MD5::Hex(device_id + "|" + stream_id + "|" + client_nonce),
            .stream_id_ = stream_id,
            .join_mode_ = "control",
            .subject_id_ = std::string("password:") +
                           MD5::Hex(remote_address + "|" + client_nonce),
            .expires_at_ms_ = 0,
            .allow_observer_ = false,
            .allow_takeover_ = transport->Settings().direct_allow_takeover,
        };
    }

    if (!transport->HasLocalRtcService()) {
        complete(make_reply(kHandlerErrNoWebRtcLocalLibrary));
        co_return;
    }
    const bool takeover_requested = [&] {
        const auto value = self->GetParam(params, "takeover");
        return value && (*value == "1" || *value == "true");
    }();
    const auto admitted_binding_id =
        std::string("rtc-local:") + authentication.stream_id_;
    const auto rtc_allocation_id = GetUUID();
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
        [weak_transport = self->transport_, admission_grant,
         admitted_binding_id, takeover_requested](
            std::function<void(LogicalSessionAdmission)> completion) {
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
        std::chrono::steady_clock::now() + std::chrono::seconds(3),
        "rtc_session_admit",
        [owner, logical_session_id = authentication.logical_session_id_,
         admitted_binding_id](const LogicalSessionAdmission& late) {
            if (late.code == LogicalSessionAdmissionCode::kAccepted) {
                if (const auto active_owner = owner.lock()) {
                    active_owner->CloseAdmittedLogicalSessionBinding(
                        logical_session_id, admitted_binding_id);
                }
            }
        });
    if (!admitted.HasValue()) {
        complete(make_reply(kHandlerErrSessionRejected));
        co_return;
    }
    const auto admission = admitted.TakeValue();
    if (admission.code != LogicalSessionAdmissionCode::kAccepted) {
        const auto code =
            admission.code == LogicalSessionAdmissionCode::kRemoteAccessDisabled
                ? kHandlerErrRemoteAccessDisabled
                : (admission.code == LogicalSessionAdmissionCode::kOccupied
                       ? kHandlerErrRtcLocalOccupied
                       : kHandlerErrSessionRejected);
        complete(make_reply(code, http::status::forbidden));
        co_return;
    }

    const auto rtc_request = std::make_shared<PxLocalRtcRequestInfo>();
    rtc_request->device_id_ = device_id;
    rtc_request->stream_id_ = authentication.stream_id_;
    rtc_request->allocation_id_ = rtc_allocation_id;
    rtc_request->req_ip_ = remote_address;
    rtc_request->sdp_ = sdp;
    rtc_request->content_type_ =
        self->GetParam(params, "content_type") ==
                std::optional<std::string>("game_stream")
            ? PxLocalRtcContentType::kGameStream
            : PxLocalRtcContentType::kDesktop;
    rtc_request->capability_enforced_ = true;
    rtc_request->takeover_ = takeover_requested;
    if (admission.role == LogicalSessionRole::kObserver) {
        rtc_request->session_role_ = PxLocalRtcSessionRole::kObserver;
        rtc_request->permissions_ = {"view", "audio"};
    } else {
        rtc_request->permissions_ = {"view", "input", "clipboard", "file",
                                     "audio"};
    }
    if (self->GetParam(params, "session_role") ==
        std::optional<std::string>("wall_observer")) {
        if (remote_address != "127.0.0.1" && remote_address != "::1") {
            self->CloseAdmittedLogicalSessionBinding(
                authentication.logical_session_id_, admitted_binding_id);
            complete(make_reply(kHandlerErrParams));
            co_return;
        }
        rtc_request->session_role_ = PxLocalRtcSessionRole::kWallObserver;
    }
    rtc_request->client_nonce_ =
        !body_nonce.empty()
            ? body_nonce
            : self->GetParam(params, "client_nonce").value_or(std::string{});

    const auto executor = co_await asio::this_coro::executor;
    const auto rtc_operation =
        PxAsyncOneShot<std::shared_ptr<PxLocalRtcReplyInfo>>::Create(executor);
    const std::weak_ptr<PxAsyncOneShot<std::shared_ptr<PxLocalRtcReplyInfo>>>
        weak_rtc_operation = rtc_operation;
    const auto allocation = transport->AllocateLocalRtcInstance(
        rtc_request, [weak_rtc_operation](
                         const std::shared_ptr<PxLocalRtcReplyInfo>& reply) {
            if (const auto operation = weak_rtc_operation.lock()) {
                if (reply) {
                    static_cast<void>(operation->TryComplete(
                        PxResult<std::shared_ptr<PxLocalRtcReplyInfo>>::Success(
                            reply)));
                } else {
                    static_cast<void>(operation->TryFail(MakePxAsyncError(
                        PxAsyncErrorCode::kProtocolError, "rtc_local_allocate",
                        "RTC reply is empty", false, "RTC_LOCAL_EMPTY_REPLY")));
                }
            }
        });
    if (allocation != PxLocalRtcAllocResult::kOk) {
        static_cast<void>(rtc_operation->TryFail(MakePxAsyncError(
            PxAsyncErrorCode::kServiceRejected, "rtc_local_allocate",
            "RTC allocation was rejected", false,
            allocation == PxLocalRtcAllocResult::kOccupied
                ? "RTC_LOCAL_OCCUPIED"
                : "RTC_LOCAL_CREATE_FAILED")));
    }
    auto rtc_reply =
        co_await PxAsyncOneShot<std::shared_ptr<PxLocalRtcReplyInfo>>::
            WaitUntil(rtc_operation, std::chrono::steady_clock::now() +
                                         std::chrono::seconds(10));
    if (!rtc_reply.HasValue()) {
        self->CloseAdmittedLogicalSessionBinding(
            authentication.logical_session_id_, admitted_binding_id);
        const auto code = rtc_reply.Error().detail_code == "RTC_LOCAL_OCCUPIED"
                              ? kHandlerErrRtcLocalOccupied
                              : kHandlerErrCreateRtcLocalServerFailed;
        LOGW(
            "event=workflow.complete component=net_ws code={} "
            "operation=rtc_local_allocate outcome=failed recoverable={} "
            "reason={}",
            rtc_reply.Error().StableCode(), rtc_reply.Error().retryable,
            rtc_reply.Error().message);
        complete(make_reply(code));
        co_return;
    }

    const auto reply_info = rtc_reply.TakeValue();
    if (console_frontend_grant && frontend_token && self->frontend_lease_renewals_) {
        self->frontend_lease_renewals_->Start(
            FrontendLeaseIdentity{
                .expected_grant = *console_frontend_grant,
                .logical_grant = admission_grant,
                .descriptor_session_id = std::move(descriptor_session_id),
                .descriptor_revision = descriptor_revision,
                .device_id = device_id,
                .stream_id = authentication.stream_id_,
                .allocation_id = rtc_allocation_id,
                .binding_id = admitted_binding_id,
            },
            std::move(frontend_token), console_frontend_grant->valid_for_ms);
    }
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
    LOGI(
        "event=session.admit component=net_ws operation=rtc_password_auth "
        "outcome=accepted device={} takeover={}",
        PrivacyLogId(device_id), rtc_request->takeover_);
    complete(DeferredHttpReply{
        .payload_ =
            self->WrapBasicInfo(200, self->GetErrorMessage(200), result),
        .status_ = http::status::ok,
    });
    co_return;
}
}  // namespace px
