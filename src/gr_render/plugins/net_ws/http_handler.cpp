//
// Created by RGAA on 2023/12/20.
//
#include "http_handler.h"
#include "tc_common_new/log.h"
#include "tc_common_new/md5.h"
#include "tc_common_new/data.h"
#include "rd_app.h"
#include "ws_plugin.h"
#include "gr_render/plugin_interface/gr_net_plugin.h"
#include "gr_render/plugin_interface/gr_plugin_events.h"

namespace tc
{

    constexpr auto kHandlerErrVerifySafetyPasswordFailed = 700;
    constexpr auto kHandlerErrNoSafetyPasswordInRenderer = 701;
    constexpr auto kHandlerErrNoRtcLocalPlugin = 702;
    constexpr auto kHandlerErrCreateRtcLocalServerFailed = 703;
    constexpr auto kHandlerErrRtcLocalOccupied = 704;

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

        auto settings = plugin_->GetPluginSettingsInfo();
        const auto& safety_pwd_md5 = value.value();
        if (safety_pwd_md5.empty() && !settings.device_safety_pwd_.empty()) {
            SendErrorJson(resp, kHandlerErrParams);
            return;
        }

        LOGI("request param, safety pwd md5: {}", safety_pwd_md5);
        if (settings.device_safety_pwd_.empty()) {
            SendOkJson(resp, "");
            //SendErrorJson(resp, kHandlerErrNoSafetyPasswordInRenderer);
            return;
        }

        LOGI("device safety pwd : {}", settings.device_safety_pwd_);
        if (settings.device_safety_pwd_ != safety_pwd_md5) {
            SendErrorJson(resp, kHandlerErrVerifySafetyPasswordFailed);
        }
        else {
            SendOkJson(resp, "");
        }
    }

    void HttpHandler::HandleGetRenderConfiguration(http::web_request& req, http::web_response& resp) {
        const auto& settings = plugin_->GetPluginSettingsInfo();
        nlohmann::json obj;
        obj["device_id"] = settings.device_id_;
        obj["relay_host"] = settings.relay_host_;
        obj["relay_port"] = std::atoi(settings.relay_port_.c_str());
        // Web 端鼠标回放需要当前采集显示器名(event_replayer 按它定位坐标系)
        obj["monitor_name"] = plugin_->GetCapturingMonitorName();
        SendOkJson(resp, obj.dump());
    }

    void HttpHandler::HandlePanelStreamMessage(http::web_request& req, http::web_response& resp) {
        auto& body = req.body();
        auto target = req.target();
        if (body.empty()) {
            SendErrorJson(resp, kHandlerErrBody);
            return;
        }

        auto event = std::make_shared<GrPluginPanelStreamMessage>();
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
        LOGI("body: {}", body);
        LOGI("req, remote: {} {} , client: {} {}",
             session_ptr->remote_address().c_str(), session_ptr->remote_port(),
             session_ptr->local_address().c_str(), session_ptr->local_port());

        // verify security password first, same as /verify/security/password
        auto params = GetQueryParams(req.query());
        if (!VerifySafetyPassword(params)) {
            resp.fill_json(WrapBasicInfo(kHandlerErrVerifySafetyPasswordFailed,
                                         GetErrorMessage(kHandlerErrVerifySafetyPasswordFailed), std::string("")),
                           http::status::forbidden);
            return;
        }

        std::string sdp;
        try {
            auto obj = nlohmann::json::parse(body);
            sdp = obj["sdp"];
        } catch(std::exception& e) {
            SendErrorJson(resp, kHandlerErrParams);
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

        // enum class GrLocalRtcContentType {
        //     kDesktop,
        //     kGameStream,
        // };
        auto content_type = [&]() -> GrLocalRtcContentType {
            if (auto param = GetParam(params, "content_type"); param.has_value()) {
                if (param.value() == "game_stream") {
                    return GrLocalRtcContentType::kGameStream;
                }
            }
            return GrLocalRtcContentType::kDesktop;
        }();

        auto rtc_req = std::make_shared<GrLocalRtcRequestInfo>();
        rtc_req->device_id_ = device_id.value();
        rtc_req->stream_id_ = stream_id.value();
        rtc_req->req_ip_ = session_ptr->remote_address();
        rtc_req->sdp_ = sdp;
        rtc_req->content_type_ = content_type;
        // takeover=1: 客户端已确认接管,直接顶掉同 stream_id 的现存连接
        if (auto param = GetParam(params, "takeover"); param.has_value()) {
            rtc_req->takeover_ = (param.value() == "1" || param.value() == "true");
        }

        std::mutex cv_mtx;
        std::condition_variable cv;
        std::shared_ptr<GrLocalRtcReplyInfo> reply_info = nullptr;
        auto r = rtc_plugin->AllocNewLocalRtcInstance(rtc_req, [&](const std::shared_ptr<GrLocalRtcReplyInfo>& reply) {
            reply_info = reply;
            cv.notify_all();
        });
        if (r == GrLocalRtcAllocResult::kOccupied) {
            SendErrorJson(resp, kHandlerErrRtcLocalOccupied);
            return;
        }
        if (r != GrLocalRtcAllocResult::kOk) {
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
        SendOkJson(static_cast<http::web_response &>(resp), obj.dump());
    }
}