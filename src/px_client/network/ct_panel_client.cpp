//
// Created by RGAA on 17/05/2025.
//

#include "ct_panel_client.h"
#include "ct_settings.h"
#include "px_common_new/log.h"
#include "ct_client_context.h"
#include "px_client_panel_message.pb.h"
#include "px_client_sdk_new/sdk_statistics.h"
#include "px_client_sdk_new/sdk_messages.h"
#include "px_common_new/message_notifier.h"
#include "px_client_panel_message.pb.h"

namespace px
{

    CtPanelClient::CtPanelClient(const std::shared_ptr<ClientContext>& ctx) {
        context_ = ctx;
    }

    void CtPanelClient::Start() {
        exiting_ = false;
        auto weak_self = weak_from_this();
        msg_listener_ = context_->ObtainMessageListener();
        msg_listener_->Listen<MsgClientFileTransmissionBegin>([weak_self](const MsgClientFileTransmissionBegin& msg) {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->context_) {
                return;
            }
            self->context_->PostTask([weak_self, msg]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                self->ReportFileTransferBegin(msg);
            });
        });

        msg_listener_->Listen<MsgClientFileTransmissionEnd>([weak_self](const MsgClientFileTransmissionEnd& msg) {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->context_) {
                return;
            }
            self->context_->PostTask([weak_self, msg]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                self->ReportFileTransferEnd(msg);
            });
        });
        msg_listener_->Listen<SdkMsgRtcIceRestartNeeded>([weak_self](const SdkMsgRtcIceRestartNeeded&) {
            if (auto self = weak_self.lock(); self && !self->exiting_ && self->context_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->RequestRtcIceRestart();
                    }
                });
            }
        });
        client_ = std::make_shared<asio2::ws_client>();
        client_->set_auto_reconnect(true);
        client_->keep_alive(true);
        client_->set_timeout(std::chrono::milliseconds(3000));
        client_->start_timer("timer_1000", 1000, [weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            self->HeartBeat();
        });

        client_->bind_init([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_) {
                return;
            }
            self->client_->ws_stream().binary(true);
            self->client_->set_no_delay(true);

        }).bind_connect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            } else {
                LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
            }

            self->client_->post_queued_event([weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                self->Hello();
            });

        }).bind_upgrade([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
            }
        }).bind_disconnect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            LOGE("CtPanelClient disconnected");
        }).bind_recv([weak_self](std::string_view data) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            auto msg = std::string(data.data(), data.size());
            self->ParseMessage(msg);
        });

        // the /ws is the websocket upgraged target
        auto settings = Settings::Instance();
        auto path = std::format("/panel?stream_id={}", settings->stream_id_);
        if (!client_->async_start("127.0.0.1", settings->panel_server_port_, path)) {
            LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
    }

    void CtPanelClient::Exit() {
        exiting_ = true;
        msg_listener_ = nullptr;
        if (client_) {
            client_->stop_all_timers();
            client_->stop();
            client_.reset();
        }
    }

    bool CtPanelClient::IsAlive() {
        return !exiting_ && client_ && client_->is_started();
    }

    void CtPanelClient::ParseMessage(std::string_view data) {
        pxcp::CpMessage cp_msg;
        if (!cp_msg.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
            LOGW("Ignore invalid panel message");
            return;
        }
        if (cp_msg.type() == pxcp::CpMessageType::kCpOpenFileTransfer) {
            LOGI("Panel requested opening file transfer in the current client");
            context_->SendAppMessage(MsgClientOpenFiletrans {});
        }
        else if (cp_msg.type() == pxcp::CpMessageType::kCpRtcIceRestart
                 && cp_msg.has_rtc_ice_restart()) {
            const auto& restart = cp_msg.rtc_ice_restart();
            LOGI("Panel requested RTC ICE restart, revision={}", restart.revision());
            context_->SendAppMessage(MsgClientRtcIceRestart {
                .connection_ticket_ = restart.connection_ticket(),
                .client_nonce_ = restart.client_nonce(),
                .instance_id_ = restart.instance_id(),
                .ice_config_json_ = restart.ice_config_json(),
                .revision_ = restart.revision(),
            });
        }
    }

    void CtPanelClient::Hello() {
        if (!IsAlive()) {
            return;
        }
        auto client = client_;
        if (!client) {
            return;
        }
        auto settings = Settings::Instance();

        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpHello);
        cp_msg.set_stream_id(settings->stream_id_);
        auto sub = cp_msg.mutable_hello();
#ifdef WIN32
        sub->set_type(pxcp::CpSessionType::kWindowsClient);
#endif
        client->async_send(cp_msg.SerializeAsString());
    }

    void CtPanelClient::HeartBeat() {
        if (!IsAlive()) {
            return;
        }
        auto client = client_;
        if (!client) {
            return;
        }

        auto stat = px::SdkStatistics::Instance();
        auto settings = Settings::Instance();

        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpHeartBeat);
        cp_msg.set_stream_id(settings->stream_id_);
        auto sub = cp_msg.mutable_heartbeat();
        sub->set_remote_device_desktop_name(stat->remote_desktop_name_.Clone());
        sub->set_remote_os_name(stat->remote_os_name_.Clone());
        client->async_send(cp_msg.SerializeAsString());
    }

    void CtPanelClient::RequestRtcIceRestart() {
        if (!IsAlive() || !client_) {
            LOGW("Cannot request RTC ICE restart: Panel channel is offline");
            return;
        }
        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpRtcIceRestartRequest);
        cp_msg.set_stream_id(Settings::Instance()->stream_id_);
        client_->async_send(cp_msg.SerializeAsString());
    }

    void CtPanelClient::ReportFileTransferBegin(const MsgClientFileTransmissionBegin& msg) {
        if (!IsAlive()) {
            return;
        }
        auto client = client_;
        if (!client) {
            return;
        }
        auto settings = Settings::Instance();
        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpFileTransferBegin);
        cp_msg.set_stream_id(settings->stream_id_);
        auto sub = cp_msg.mutable_ft_transfer_beg();
        sub->set_the_file_id(msg.the_file_id_);
        sub->set_begin_timestamp(msg.begin_timestamp_);
        sub->set_direction(msg.direction_);
        sub->set_file_detail(msg.file_detail_);
        sub->set_remote_device_id(msg.remote_device_id_);
        client->async_send(cp_msg.SerializeAsString());
    }

    void CtPanelClient::ReportFileTransferEnd(const MsgClientFileTransmissionEnd& msg) {
        if (!IsAlive()) {
            return;
        }
        auto client = client_;
        if (!client) {
            return;
        }
        auto settings = Settings::Instance();
        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpFileTransferEnd);
        cp_msg.set_stream_id(settings->stream_id_);
        auto& terminal = *cp_msg.mutable_ft_transfer_end();
        terminal.set_the_file_id(msg.the_file_id_);
        terminal.set_end_timestamp(msg.end_timestamp_);
        terminal.set_success(msg.success_);
        terminal.set_status(msg.status_);
        terminal.set_end_reason(msg.end_reason_);
        client->async_send(cp_msg.SerializeAsString());
    }

}
