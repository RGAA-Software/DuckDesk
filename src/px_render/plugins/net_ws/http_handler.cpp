//
// Created by RGAA on 2023/12/20.
//
#include "http_handler.h"
#include "version_config.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_common_new/data.h"
#include "rd_app.h"
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

    struct TicketRedeemWaitState {
        std::mutex mutex_;
        std::condition_variable cv_;
        bool completed_ = false;
        bool ok_ = false;
        std::string code_;
        std::vector<std::string> permissions_;
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
        return BaseHandler::GetErrorMessage(code);
    }

    void HttpHandler::HandlePing(http::web_request &req, http::web_response &resp) {
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

    void HttpHandler::HandleAllocLocalRtc(std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& resp) {
        auto& body = req.body();
        auto target = req.target();

        LOGI("req host:port, {}:{}, target: {}", req.host(), req.port(), target);
        LOGI("req, remote: {} {} , client: {} {}",
             session_ptr->remote_address().c_str(), session_ptr->remote_port(),
             session_ptr->local_address().c_str(), session_ptr->local_port());

        auto params = GetQueryParams(req.query());
        std::string sdp;
        std::string ticket;
        std::string body_nonce;
        std::string body_instance_id;
        try {
            auto obj = nlohmann::json::parse(body);
            sdp = obj["sdp"];
            ticket = obj.value("ticket", "");
            body_nonce = obj.value("client_nonce", "");
            body_instance_id = obj.value("instance_id", "");
        } catch(std::exception& e) {
            SendErrorJson(resp, kHandlerErrParams);
            return;
        }

        std::vector<std::string> ticket_permissions;
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
                const std::vector<std::string>& permissions) {
                {
                    std::scoped_lock lock(wait_state->mutex_);
                    wait_state->ok_ = ok;
                    wait_state->code_ = code;
                    wait_state->permissions_ = permissions;
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
            if (std::find(ticket_permissions.begin(), ticket_permissions.end(), "view") == ticket_permissions.end()) {
                LOGW("Connection ticket rejected: missing view capability");
                resp.fill_json(
                    WrapBasicInfo(
                        kHandlerErrConnectionTicketRejected,
                        GetErrorMessage(kHandlerErrConnectionTicketRejected),
                        std::string("")),
                    http::status::forbidden);
                return;
            }
        }
        else if (!VerifySafetyPassword(params)) {
            // Static passwords remain only for the explicit manual/debug path.
            resp.fill_json(WrapBasicInfo(kHandlerErrVerifySafetyPasswordFailed,
                                         GetErrorMessage(kHandlerErrVerifySafetyPasswordFailed), std::string("")),
                           http::status::forbidden);
            return;
        }

        auto device_id = GetParam(params, "device_id");
        auto stream_id = GetParam(params, "stream_id");
        if (!device_id.has_value() || !stream_id.has_value() || sdp.empty()) {
            SendErrorJson(resp, kHandlerErrParams);
            return;
        }

        auto rtc_plugin = this->plugin_->GetLocalRtcPlugin();
        if (rtc_plugin == nullptr) {
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
        rtc_req->device_id_ = device_id.value();
        rtc_req->stream_id_ = stream_id.value();
        rtc_req->req_ip_ = session_ptr->remote_address();
        rtc_req->sdp_ = sdp;
        rtc_req->content_type_ = content_type;
        rtc_req->capability_enforced_ = !ticket.empty();
        rtc_req->permissions_ = ticket_permissions;
        // wall_observer is accepted only after the safety password validation
        // above. CMS keeps that credential server-side and proxies signaling;
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

        std::mutex cv_mtx;
        std::condition_variable cv;
        std::shared_ptr<PxLocalRtcReplyInfo> reply_info = nullptr;
        auto r = rtc_plugin->AllocNewLocalRtcInstance(rtc_req, [&](const std::shared_ptr<PxLocalRtcReplyInfo>& reply) {
            reply_info = reply;
            cv.notify_all();
        });
        if (r == PxLocalRtcAllocResult::kOccupied) {
            SendErrorJson(resp, kHandlerErrRtcLocalOccupied);
            return;
        }
        if (r != PxLocalRtcAllocResult::kOk) {
            SendErrorJson(resp, kHandlerErrCreateRtcLocalServerFailed);
            return;
        }

        // wait
        std::unique_lock lk(cv_mtx);
        cv.wait_for(lk, std::chrono::seconds(10));

        if (!reply_info) {
            SendErrorJson(resp, kHandlerErrCreateRtcLocalServerFailed);
            return;
        }

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
        SendOkJson(static_cast<http::web_response &>(resp), obj.dump());
    }
}
