//
// Created by RGAA on 2023/12/20.
//
#include "http_handler.h"
#include "version_config.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_common_new/data.h"
#include "px_common_new/uuid.h"
#include "ws_plugin.h"
#include "px_render/plugin_interface/px_net_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace px
{

    constexpr auto kHandlerErrVerifySafetyPasswordFailed = 700;
    constexpr auto kHandlerErrNoSafetyPasswordInRenderer = 701;
    constexpr auto kHandlerErrNoRtcLocalPlugin = 702;
    constexpr auto kHandlerErrCreateRtcLocalServerFailed = 703;
    constexpr auto kHandlerErrRtcLocalOccupied = 704;
    constexpr auto kHandlerErrConnectionTicketRejected = 705;
    constexpr auto kHandlerErrDirectGrantRejected = 706;
    constexpr auto kHandlerErrIpDirectAuthorizationRejected = 707;

    int64_t CurrentSystemMilliseconds() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::string DirectAuditSubject(const DirectSessionGrantBinding& binding) {
        // The persistent plugin log identifies the direct subject without
        // retaining the nonce, token, password, offer SDP, or full address.
        return MD5::Hex(binding.device_id_ + "|" + binding.client_nonce_
                        + "|" + binding.remote_address_);
    }

    struct TicketRedeemWaitState {
        std::mutex mutex_;
        std::condition_variable cv_;
        bool completed_ = false;
        bool ok_ = false;
        std::string code_;
        std::vector<std::string> permissions_;
        std::string logical_session_id_;
        std::string stream_id_;
        std::string join_mode_;
        std::string subject_id_;
        int64_t expires_at_ms_ = 0;
        bool allow_observer_ = true;
        bool allow_takeover_ = true;
    };

    struct SessionAdmissionWaitState {
        std::mutex mutex_;
        std::condition_variable cv_;
        bool completed_ = false;
        LogicalSessionAdmission admission_;
    };

    struct LocalRtcReplyWaitState {
        std::mutex mutex_;
        std::condition_variable cv_;
        std::shared_ptr<PxLocalRtcReplyInfo> reply_;
    };

    HttpHandler::HttpHandler(WsPlugin* plugin) {
        this->plugin_ = plugin;
    }

    std::string HttpHandler::GetErrorMessage(int code) {
        if (code == kHandlerErrVerifySafetyPasswordFailed) {
            return "Verify security password failed";
        }
        else if (code == kHandlerErrNoSafetyPasswordInRenderer) {
            return "No security password in renderer";
        }
        else if (code == kHandlerErrNoRtcLocalPlugin) {
            return "No RtcLocalPlugin";
        }
        else if (code == kHandlerErrCreateRtcLocalServerFailed) {
            return "Create Rtc local server failed";
        }
        else if (code == kHandlerErrRtcLocalOccupied) {
            return "Rtc local connection occupied";
        }
        else if (code == kHandlerErrConnectionTicketRejected) {
            return "Connection ticket rejected";
        }
        else if (code == kHandlerErrDirectGrantRejected) {
            return "Direct session grant rejected";
        }
        else if (code == kHandlerErrIpDirectAuthorizationRejected) {
            return "IP direct authorization rejected";
        }
        return BaseHandler::GetErrorMessage(code);
    }

    void HttpHandler::HandlePing(http::web_request &req, http::web_response &resp) {
        auto data = WrapBasicInfo(200, "ok", std::string("Pong"));
        resp.fill_json(data);
    }

    void HttpHandler::HandleVerifySecurityPassword(
        const std::shared_ptr<asio2::http_session>& session,
        http::web_request& req,
        http::web_response& resp) {
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
        // The Panel validates an id-less IP-direct password before it starts
        // px_client. Reserve the normal one-time stream id here, so the child
        // only connects to that prepared stream and never receives a password
        // or a second authorization credential.
        const auto client_nonce = GetParam(params, "client_nonce").value_or(std::string{});
        if (!client_nonce.empty() && session) {
            DirectSessionGrantBinding binding{
                .device_id_ = {},
                .stream_id_ = {},
                .client_nonce_ = client_nonce,
                .remote_address_ = session->remote_address(),
            };
            const auto now_ms = CurrentSystemMilliseconds();
            nlohmann::json result;
            result["stream_id"] = direct_session_grants_.IssueStreamBinding(
                std::move(binding), now_ms);
            result["expires_at_ms"] = now_ms + DirectSessionGrantStore::kLifetimeMilliseconds;
            SendOkJson(resp, result.dump());
            return;
        }
        SendOkJson(resp, "");
    }

    void HttpHandler::HandleGetRenderConfiguration(http::web_request& req, http::web_response& resp) {
        const auto& settings = plugin_->GetPluginSettingsInfo();
        nlohmann::json obj;
        obj["device_id"] = settings.device_id_;
        obj["relay_host"] = settings.relay_host_;
        obj["relay_port"] = std::atoi(settings.relay_port_.c_str());
        // Web 端鼠标回放需要当前采集显示器名(event_replayer 按它定位坐标系)
        obj["monitor_name"] = plugin_->GetCapturingMonitorName();
        // 供 Web 客户端展示,便于确认被控端是否为旧版本
        obj["app_version"] = PROJECT_VERSION;
        SendOkJson(resp, obj.dump());
    }

    void HttpHandler::HandlePanelStreamMessage(http::web_request& req, http::web_response& resp) {
        auto& body = req.body();
        auto target = req.target();
        if (body.empty()) {
            SendErrorJson(resp, kHandlerErrBody);
            return;
        }

        auto event = std::make_shared<PxPluginPanelStreamMessage>();
        event->body_ = Data::From(body);
        this->plugin_->CallbackEvent(event);

        SendOkJson(resp, "");
    }

    bool HttpHandler::VerifySafetyPassword(const std::unordered_map<std::string, std::string>& params) {
        auto settings = plugin_->GetPluginSettingsInfo();
        if (settings.device_safety_pwd_.empty() && settings.device_random_pwd_.empty()) {
            return true;
        }
        auto value = GetParam(params, "safety_pwd_md5");
        if (!value.has_value() || value.value().empty()) {
            return false;
        }
        // 安全密码:存的就是 MD5,直接比对
        if (!settings.device_safety_pwd_.empty() && settings.device_safety_pwd_ == value.value()) {
            return true;
        }
        // 临时(随机)密码:存的是明文,兼容"前端 md5 后传入"和"直接传明文"两种形式
        if (!settings.device_random_pwd_.empty()) {
            if (settings.device_random_pwd_ == value.value()) {
                return true;
            }
            if (MD5::Hex(settings.device_random_pwd_) == value.value()) {
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
        const auto event = std::make_shared<PxPluginCloseLogicalSessionBindingEvent>();
        event->logical_session_id_ = logical_session_id;
        event->binding_id_ = binding_id;
        plugin_->CallbackEvent(event);
    }

    void HttpHandler::HandleAllocLocalRtc(std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& resp) {
        auto& body = req.body();
        // This endpoint can carry a password digest in its query string. Keep
        // credentials out of persistent logs; the handler name is sufficient.
        LOGI("req host:port, {}:{}, target: /alloc/local/rtc", req.host(), req.port());
        LOGI("req, remote: {} {} , client: {} {}",
             session_ptr->remote_address().c_str(), session_ptr->remote_port(),
             session_ptr->local_address().c_str(), session_ptr->local_port());

        auto params = GetQueryParams(req.query());
        std::string sdp;
        std::string ticket;
        std::string body_nonce;
        std::string body_instance_id;
        std::string direct_session_grant;
        try {
            auto obj = nlohmann::json::parse(body);
            sdp = obj["sdp"];
            ticket = obj.value("ticket", "");
            body_nonce = obj.value("client_nonce", "");
            body_instance_id = obj.value("instance_id", "");
            direct_session_grant = obj.value("direct_session_grant", "");
        } catch(std::exception& e) {
            SendErrorJson(resp, kHandlerErrParams);
            return;
        }

        const auto device_id = GetParam(params, "device_id").value_or(std::string{});
        if (sdp.empty()) {
            SendErrorJson(resp, kHandlerErrParams);
            return;
        }
        // A missing device id unambiguously identifies password-only IP
        // direct access. Do not substitute Render's configured Console id:
        // doing so would silently turn a simple LAN password check into a
        // Console/direct-grant identity flow the caller never requested.
        const bool password_only_ip_direct = ticket.empty() && device_id.empty();
        if (!ticket.empty() && device_id.empty()) {
            SendErrorJson(resp, kHandlerErrParams);
            return;
        }

        std::vector<std::string> ticket_permissions;
        std::string ticket_logical_session_id;
        std::string ticket_stream_id;
        std::string ticket_join_mode;
        std::string ticket_subject_id;
        int64_t ticket_expires_at_ms = 0;
        bool ticket_allow_observer = true;
        bool ticket_allow_takeover = true;
        if (!ticket.empty()) {
            if (body_nonce.empty()) {
                SendErrorJson(resp, kHandlerErrParams);
                return;
            }
            auto wait_state = std::make_shared<TicketRedeemWaitState>();
            auto event = std::make_shared<PxPluginRedeemConnectionTicketEvent>();
            event->ticket_ = ticket;
            event->client_nonce_ = body_nonce;
            event->instance_id_ = body_instance_id;
            event->callback_ = [wait_state](
                bool ok,
                const std::string& code,
                const std::vector<std::string>& permissions,
                const std::string&,
                const std::string& logical_session_id,
                const std::string& stream_id,
                const std::string& join_mode,
                const std::string& subject_id,
                const int64_t expires_at_ms,
                const bool allow_observer,
                const bool allow_takeover) {
                {
                    std::scoped_lock lock(wait_state->mutex_);
                    wait_state->ok_ = ok;
                    wait_state->code_ = code;
                    wait_state->permissions_ = permissions;
                    wait_state->logical_session_id_ = logical_session_id;
                    wait_state->stream_id_ = stream_id;
                    wait_state->join_mode_ = join_mode;
                    wait_state->subject_id_ = subject_id;
                    wait_state->expires_at_ms_ = expires_at_ms;
                    wait_state->allow_observer_ = allow_observer;
                    wait_state->allow_takeover_ = allow_takeover;
                    wait_state->completed_ = true;
                }
                wait_state->cv_.notify_all();
            };
            plugin_->CallbackEvent(event);
            std::unique_lock lock(wait_state->mutex_);
            wait_state->cv_.wait_for(lock, std::chrono::seconds(3), [&] {
                return wait_state->completed_;
            });
            if (!wait_state->completed_ || !wait_state->ok_) {
                LOGW("Connection ticket rejected: {}", wait_state->code_);
                resp.fill_json(
                    WrapBasicInfo(
                        kHandlerErrConnectionTicketRejected,
                        GetErrorMessage(kHandlerErrConnectionTicketRejected),
                        std::string("")),
                    http::status::forbidden);
                return;
            }
            ticket_permissions = wait_state->permissions_;
            ticket_logical_session_id = wait_state->logical_session_id_;
            ticket_stream_id = wait_state->stream_id_;
            ticket_join_mode = wait_state->join_mode_;
            ticket_subject_id = wait_state->subject_id_;
            ticket_expires_at_ms = wait_state->expires_at_ms_;
            ticket_allow_observer = wait_state->allow_observer_;
            ticket_allow_takeover = wait_state->allow_takeover_;
            const bool may_view = std::find(ticket_permissions.begin(), ticket_permissions.end(), "view") != ticket_permissions.end();
            const bool may_transfer_files = std::find(ticket_permissions.begin(), ticket_permissions.end(), "file") != ticket_permissions.end();
            if ((!may_view && !may_transfer_files) || ticket_logical_session_id.empty()
                || ticket_stream_id.empty() || ticket_join_mode.empty()) {
                LOGW("Connection ticket rejected: missing view or file capability");
                resp.fill_json(
                    WrapBasicInfo(
                        kHandlerErrConnectionTicketRejected,
                        GetErrorMessage(kHandlerErrConnectionTicketRejected),
                        std::string("")),
                    http::status::forbidden);
                return;
            }
        }
        bool direct_access = false;
        DirectSessionGrantBinding direct_grant_binding;
        std::string direct_issued_stream_id;
        if (password_only_ip_direct) {
            const auto nonce_param = GetParam(params, "client_nonce");
            const auto route_seed = !body_nonce.empty() ? body_nonce
                : (nonce_param.has_value() && !nonce_param->empty()
                    ? nonce_param.value() : GetUUID());
            const auto requested_stream_id = GetParam(params, "stream_id")
                .value_or(std::string{});
            const DirectSessionGrantBinding auth_binding{
                .device_id_ = {},
                .stream_id_ = requested_stream_id,
                .client_nonce_ = route_seed,
                .remote_address_ = session_ptr->remote_address(),
            };
            if (!requested_stream_id.empty()
                && direct_session_grants_.Redeem(
                    requested_stream_id, auth_binding, CurrentSystemMilliseconds())) {
                direct_issued_stream_id = requested_stream_id;
            }
            else if (VerifySafetyPassword(params)) {
                // Compatibility for standalone/older clients. Panel-launched
                // clients always use a stream prepared before process launch.
                direct_issued_stream_id = std::string("ip-direct:") + MD5::Hex(
                    session_ptr->remote_address() + std::string("|") + route_seed);
            }
            else {
                LOGW("IP direct prepared stream rejected");
                const auto code = requested_stream_id.empty()
                    ? kHandlerErrVerifySafetyPasswordFailed
                    : kHandlerErrIpDirectAuthorizationRejected;
                resp.fill_json(WrapBasicInfo(code, GetErrorMessage(code), std::string("")),
                               http::status::forbidden);
                return;
            }
            ticket_logical_session_id = direct_issued_stream_id;
            ticket_stream_id = direct_issued_stream_id;
            ticket_join_mode = "control";
            ticket_subject_id = std::string("ip-direct:") + MD5::Hex(
                session_ptr->remote_address() + std::string("|") + route_seed);
            ticket_expires_at_ms = CurrentSystemMilliseconds()
                + std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::minutes(5)).count();
            const auto local_settings = plugin_->GetPluginSettingsInfo();
            ticket_allow_observer = false;
            ticket_allow_takeover = local_settings.direct_allow_takeover_;
            direct_access = true;
        }
        else if (ticket.empty()) {
            const auto nonce_param = GetParam(params, "client_nonce");
            const auto direct_nonce = !body_nonce.empty() ? body_nonce
                : (nonce_param.has_value() ? nonce_param.value() : std::string{});
            if (direct_nonce.empty()) {
                SendErrorJson(resp, kHandlerErrParams);
                return;
            }
            direct_access = true;
            direct_issued_stream_id = std::string("direct:") + MD5::Hex(
                session_ptr->remote_address() + std::string("|") + device_id
                + "|" + direct_nonce);
            direct_grant_binding = {
                .device_id_ = device_id,
                .stream_id_ = direct_issued_stream_id,
                .client_nonce_ = direct_nonce,
                .remote_address_ = session_ptr->remote_address(),
            };
            if (direct_session_grant.empty()) {
                if (!VerifySafetyPassword(params)) {
                    LOGW("Direct RTC audit: result=initial_auth_rejected device={} subject_hash={}",
                         device_id, DirectAuditSubject(direct_grant_binding));
                    resp.fill_json(WrapBasicInfo(kHandlerErrVerifySafetyPasswordFailed,
                                                 GetErrorMessage(kHandlerErrVerifySafetyPasswordFailed), std::string("")),
                                   http::status::forbidden);
                    return;
                }
            }
            else if (!direct_session_grants_.Redeem(
                direct_session_grant, direct_grant_binding, CurrentSystemMilliseconds())) {
                LOGW("Direct RTC audit: result=grant_rejected device={} subject_hash={}",
                     device_id, DirectAuditSubject(direct_grant_binding));
                resp.fill_json(WrapBasicInfo(kHandlerErrDirectGrantRejected,
                                             GetErrorMessage(kHandlerErrDirectGrantRejected), std::string("")),
                               http::status::forbidden);
                return;
            }
        }
        if (ticket.empty() && !password_only_ip_direct) {
            ticket_logical_session_id = direct_issued_stream_id;
            ticket_stream_id = direct_issued_stream_id;
            ticket_join_mode = "control";
            ticket_subject_id = std::string("direct:") + MD5::Hex(
                session_ptr->remote_address() + std::string("|") + direct_grant_binding.client_nonce_);
            ticket_expires_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
                + std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::minutes(5)).count();
            const auto local_settings = plugin_->GetPluginSettingsInfo();
            ticket_allow_observer = false;
            ticket_allow_takeover = local_settings.direct_allow_takeover_;
        }

        if (!plugin_->HasLocalRtcService()) {
            SendErrorJson(resp, kHandlerErrNoRtcLocalPlugin);
            return;
        }

        // enum class PxLocalRtcContentType {
        //     kDesktop,
        //     kGameStream,
        // };
        auto content_type = [&]() -> PxLocalRtcContentType {
            if (auto param = GetParam(params, "content_type"); param.has_value()) {
                if (param.value() == "game_stream") {
                    return PxLocalRtcContentType::kGameStream;
                }
            }
            return PxLocalRtcContentType::kDesktop;
        }();

        auto rtc_req = std::make_shared<PxLocalRtcRequestInfo>();
        rtc_req->device_id_ = device_id;
        // The Console or direct grant owns the route identity.  The historical
        // query stream_id remains non-authoritative compatibility input and
        // must never select a different logical session.
        rtc_req->stream_id_ = ticket_stream_id;
        rtc_req->req_ip_ = session_ptr->remote_address();
        rtc_req->sdp_ = sdp;
        rtc_req->content_type_ = content_type;
        rtc_req->capability_enforced_ = !ticket.empty();
        rtc_req->permissions_ = ticket_permissions;
        std::string admitted_logical_session_id;
        std::string admitted_binding_id;
        {
            const auto takeover_value = GetParam(params, "takeover");
            const bool takeover_requested = takeover_value.has_value()
                && (takeover_value.value() == "1" || takeover_value.value() == "true");
            const auto wait_state = std::make_shared<SessionAdmissionWaitState>();
            const auto admission_event = std::make_shared<PxPluginAdmitLogicalSessionEvent>();
            admission_event->grant_ = {
                .logical_session_id = ticket_logical_session_id,
                .stream_id = ticket_stream_id,
                .subject_id = ticket_subject_id,
                .join_mode = ticket_join_mode,
                .expires_at_ms = ticket_expires_at_ms,
                .allow_observer = ticket_allow_observer,
                .allow_takeover = ticket_allow_takeover,
            };
            admission_event->transport_ = LogicalSessionTransport::kRtcLocal;
            admission_event->binding_id_ = std::string("rtc-local:") + ticket_stream_id;
            admission_event->takeover_ = takeover_requested;
            admission_event->callback_ = [wait_state](const LogicalSessionAdmission admission) {
                std::scoped_lock lock(wait_state->mutex_);
                wait_state->admission_ = admission;
                wait_state->completed_ = true;
                wait_state->cv_.notify_all();
            };
            plugin_->CallbackEvent(admission_event);
            std::unique_lock admission_lock(wait_state->mutex_);
            wait_state->cv_.wait_for(admission_lock, std::chrono::seconds(3), [&] {
                return wait_state->completed_;
            });
            if (!wait_state->completed_) {
                SendErrorJson(resp, kHandlerErrConnectionTicketRejected);
                return;
            }
            const auto admission = wait_state->admission_;
            if (admission.code != LogicalSessionAdmissionCode::kAccepted) {
                const auto code = admission.code == LogicalSessionAdmissionCode::kOccupied
                    ? kHandlerErrRtcLocalOccupied : kHandlerErrConnectionTicketRejected;
                if (direct_access && !password_only_ip_direct) {
                    LOGW("Direct RTC audit: result=admission_rejected device={} subject_hash={} code={}",
                         device_id, DirectAuditSubject(direct_grant_binding), code);
                }
                resp.fill_json(WrapBasicInfo(code, GetErrorMessage(code), std::string("")),
                               http::status::forbidden);
                return;
            }
            admitted_logical_session_id = ticket_logical_session_id;
            admitted_binding_id = admission_event->binding_id_;
            rtc_req->stream_id_ = ticket_stream_id;
            rtc_req->takeover_ = takeover_requested;
            if (admission.role == LogicalSessionRole::kObserver) {
                rtc_req->session_role_ = PxLocalRtcSessionRole::kObserver;
                rtc_req->permissions_ = {"view", "audio"};
            } else {
                rtc_req->permissions_ = {"view", "input", "clipboard", "file", "audio"};
            }
        }
        // wall_observer is accepted only after the safety password validation
        // above. Console keeps that credential server-side and proxies signaling;
        // browsers never need to receive it.
        if (auto param = GetParam(params, "session_role");
            param.has_value() && param.value() == "wall_observer") {
            // Hidden/no-audit observer is privileged. Only px_service on this
            // machine may request it; remote callers with a device password
            // still get the normal, visible interactive role.
            const auto remote = session_ptr->remote_address();
            if (remote != "127.0.0.1" && remote != "::1") {
                LOGW("Reject remote wall_observer request from {}", remote);
                SendErrorJson(resp, kHandlerErrParams);
                return;
            }
            rtc_req->session_role_ = PxLocalRtcSessionRole::kWallObserver;
        }
        // takeover=1: 客户端已确认接管,直接顶掉同 stream_id 的现存连接
        if (auto param = GetParam(params, "takeover"); param.has_value()) {
            rtc_req->takeover_ = (param.value() == "1" || param.value() == "true");
        }
        // client_nonce: web client 的浏览器标识,同 nonce 自动接管见
        // RtcLocalPlugin::AllocNewLocalRtcInstance 的占用判断
        if (auto param = GetParam(params, "client_nonce"); param.has_value()) {
            rtc_req->client_nonce_ = param.value();
        }
        if (!body_nonce.empty()) {
            rtc_req->client_nonce_ = body_nonce;
        }

        const auto reply_wait_state = std::make_shared<LocalRtcReplyWaitState>();
        const auto r = plugin_->AllocateLocalRtcInstance(rtc_req, [reply_wait_state](
            const std::shared_ptr<PxLocalRtcReplyInfo>& reply) {
            {
                std::scoped_lock lock(reply_wait_state->mutex_);
                reply_wait_state->reply_ = reply;
            }
            reply_wait_state->cv_.notify_all();
        });
        if (r == PxLocalRtcAllocResult::kOccupied) {
            CloseAdmittedLogicalSessionBinding(admitted_logical_session_id, admitted_binding_id);
            SendErrorJson(resp, kHandlerErrRtcLocalOccupied);
            return;
        }
        if (r != PxLocalRtcAllocResult::kOk) {
            CloseAdmittedLogicalSessionBinding(admitted_logical_session_id, admitted_binding_id);
            SendErrorJson(resp, kHandlerErrCreateRtcLocalServerFailed);
            return;
        }

        // wait
        std::unique_lock reply_lock(reply_wait_state->mutex_);
        reply_wait_state->cv_.wait_for(reply_lock, std::chrono::seconds(10), [&] {
            return reply_wait_state->reply_ != nullptr;
        });

        if (!reply_wait_state->reply_) {
            SendErrorJson(resp, kHandlerErrCreateRtcLocalServerFailed);
            return;
        }
        const auto reply_info = reply_wait_state->reply_;

        nlohmann::json obj;
        obj["answer_sdp"] = reply_info->answer_sdp_;
        // 显示器列表(与 video track 同序):多 track 客户端做 track→mon_name 映射,
        // 并据此决定下次 offer 声明几条 video m-line;web/旧客户端忽略此字段
        auto monitors = nlohmann::json::array();
        int mon_index = 0;
        for (const auto& m : reply_info->monitors_) {
            monitors.push_back({
                {"name", m.name_},
                {"width", m.width_},
                {"height", m.height_},
                {"left", m.left_},
                {"top", m.top_},
                {"right", m.right_},
                {"bottom", m.bottom_},
                {"index", mon_index++},
            });
        }
        obj["monitors"] = monitors;
        if (password_only_ip_direct) {
            obj["stream_id"] = direct_issued_stream_id;
            LOGI("IP direct password authentication admitted");
        }
        else if (direct_access) {
            const auto now_ms = CurrentSystemMilliseconds();
            obj["stream_id"] = direct_issued_stream_id;
            obj["direct_session_grant"] = direct_session_grants_.Issue(direct_grant_binding, now_ms);
            obj["direct_session_grant_expires_at_ms"] = now_ms
                + DirectSessionGrantStore::kLifetimeMilliseconds;
            LOGI("Direct RTC audit: result=admitted device={} subject_hash={} takeover={}",
                 device_id, DirectAuditSubject(direct_grant_binding), rtc_req->takeover_);
        }
        SendOkJson(static_cast<http::web_response &>(resp), obj.dump());
    }
}
