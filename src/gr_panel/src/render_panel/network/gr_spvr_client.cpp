//
// Created by RGAA on 17/05/2025.
//

#include "gr_spvr_client.h"
#include "spvr_panel.pb.h"
#include "tc_common_new/log.h"
#include "render_panel/gr_context.h"
#include "tc_common_new/message_notifier.h"
#include "render_panel/gr_app_messages.h"
#include "tc_common_new/time_util.h"
#include "spvr_panel.pb.h"
#include "render_panel/gr_settings.h"
#include "tc_common_new/base64.h"
#include "hw_info/hw_info.h"
#include <nlohmann/json.hpp>
#include "render_panel/gr_application.h"
#include "render_panel/user/gr_user_manager.h"
#include "network/ct_auth_token.h"

using namespace spvr_panel;

namespace tc
{
    GrSpvrClient::GrSpvrClient(const std::shared_ptr<GrContext>& ctx,
                               const std::string& host,
                               int port,
                               const std::string& device_id) {
        settings_ = GrSettings::Instance();
        context_ = ctx;
        host_ = host;
        port_ = port;
        device_id_ = device_id;
    }

    void GrSpvrClient::Start() {
        auto weak_self = weak_from_this();

        msg_listener_ = context_->ObtainMessageListener();
        msg_listener_->Listen<MsgGrTimer1S>([weak_self](const MsgGrTimer1S& m) {
            auto self = weak_self.lock();
            if (!self || !self->context_) {
                return;
            }
            self->context_->PostTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                self->Heartbeat();
            });
        });

        msg_listener_->Listen<MsgHWInfo>([weak_self](const MsgHWInfo& info) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->sys_info_ = info.sys_info_;
        });

        client_ = std::make_shared<asio2::wss_client>();
        client_->set_auto_reconnect(true);
        client_->keep_alive(true);
        client_->set_timeout(std::chrono::milliseconds(3000));
        client_->set_verify_mode(asio::ssl::verify_none);

        client_->bind_init([weak_self]() {
            auto self = weak_self.lock();
            if (!self || !self->client_) {
                return;
            }
            self->client_->ws_stream().binary(true);
            self->client_->set_no_delay(true);

            // Generate a fresh token for every connection attempt (including auto reconnect).
            // The token has a short lifetime (60s), so reusing the original path on reconnect
            // would cause the CMS token filter to reject the connection.
            auto user_id = grApp->GetUserManager()->GetUserId();
            auto token = GenerateConnectionToken(grApp->GetAppkey());
            auto path = std::format("/spvr/panel?appkey={}&token={}&ts={}&nonce={}&device_id={}&user_id={}",
                                     grApp->GetAppkey(), token.token, token.ts, token.nonce, self->device_id_, user_id);
            self->client_->set_upgrade_target(path);
        })
        .bind_connect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || !self->client_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            } else {
                LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
            }

            self->client_->post_queued_event([weak_self]() {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                self->Hello();
            });

        })
        .bind_upgrade([]() {
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
            }
        })
        .bind_disconnect([weak_self]() {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            LOGE("*** Disconnected for spvr-client: {}", self->device_id_);
        })
        .bind_recv([weak_self](std::string_view data) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            auto msg = std::string(data.data(), data.size());
            self->ParseMessage(msg);
        });

        LOGI("will connect => {}:{}/spvr/panel", host_, port_);
        if (!client_->async_start(host_, port_)) {
            LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
    }

    void GrSpvrClient::Stop() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
        if (client_) {
            client_->stop_all_timers();
            client_->stop();
            client_.reset();
        }
    }

    bool GrSpvrClient::IsStarted() {
        return client_ != nullptr;
    }

    bool GrSpvrClient::IsActive() {
        return IsStarted() && client_->is_started();
    }

    void GrSpvrClient::Hello() {
        if (!IsActive()) {
            return;
        }
        spvr_panel::SpvrPanelMessage msg;
        msg.set_msg_type(spvr_panel::SpvrPanelMessageType::kSpvrPanelHello);
        auto sub = msg.mutable_hello();
        sub->set_device_id(device_id_);
        auto user_id = grApp->GetUserManager()->GetUserId();
        sub->set_user_id(user_id);
        sub->set_device_name(settings_->GetDeviceName());
        PostBinMessage(msg.SerializeAsString());
    }

    void GrSpvrClient::Heartbeat() {
        if (!IsActive()) {
            return;
        }

        auto ips = context_->GetIps();
        auto desktop_link_raw = context_->MakeDesktopLinkMessage(ips);
        auto desktop_link = std::format("link://{}", Base64::Base64Encode(desktop_link_raw));

        spvr_panel::SpvrPanelMessage msg;
        msg.set_msg_type(spvr_panel::SpvrPanelMessageType::kSpvrPanelHeartBeat);
        auto sub = msg.mutable_heartbeat();
        sub->set_hb_index(hb_idx_++);
        sub->set_device_id(device_id_);
        sub->set_desktop_link(desktop_link);
        sub->set_desktop_link_raw(desktop_link_raw);
        auto user_id = grApp->GetUserManager()->GetUserId();
        sub->set_user_id(user_id);
        if (auto sys_info = sys_info_.Clone(); sys_info != nullptr) {
            try {
                auto obj = nlohmann::json::parse(sys_info->raw_json_msg_);
                obj["cpu"]["current_frequency"] = sys_info->cpu_.current_frequency_;
                sub->set_sys_info_raw(obj.dump());
            }
            catch (...) {
                sub->set_sys_info_raw(sys_info->raw_json_msg_);
            }

            //LOGI("Heartbeat sys infor raw: {}", sys_info->raw_json_msg_);
        }
        if (!ips.empty()) {
            sub->set_device_ip_addr(ips[0].ip_addr_);
        }
        sub->set_device_name(settings_->GetDeviceName());
        PostBinMessage(msg.SerializeAsString());
    }

    void GrSpvrClient::PostBinMessage(const std::string& m) {
        if (IsActive()) {
            client_->async_send(m);
        }
    }

    void GrSpvrClient::ParseMessage(const std::string& m) {
        auto pm = std::make_shared<spvr_panel::SpvrPanelMessage>();
        bool r = pm->ParsePartialFromString(m);
        if (!r) {
            LOGE("Parse SpvrClient message failed!");
            return;
        }
        last_received_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();

        auto type = pm->msg_type();
        if (type == SpvrPanelMessageType::kSpvrPanelHello) {
            LOGI("SpvrClient hello.");
        }
        else if (type == SpvrPanelMessageType::kSpvrPanelHeartBeat) {
            //LOGI("SpvrClient heartbeat.");
        }
    }

    bool GrSpvrClient::IsAlive() const {
        auto current_timestamp = TimeUtil::GetCurrentTimestamp();
        auto diff = current_timestamp - last_received_timestamp_ < 3100;
        //LOGI("Diff alive: {}", diff);
        return diff;
    }

}
