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
#include "px_common_new/async_delay.h"
#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/reconnect_supervisor.h"
#include "px_common_new/websocket_reconnect_adapter.h"
#include "px_client_panel_message.pb.h"

namespace px
{

    CtPanelClient::CtPanelClient(const std::shared_ptr<ClientContext>& ctx) {
        context_ = ctx;
    }

    CtPanelClient::~CtPanelClient() {
        Exit();
    }

    void CtPanelClient::Start() {
        if (!exiting_ && client_) {
            return;
        }
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
        msg_listener_->Listen<SdkMsgNetworkConnected>([weak_self](const SdkMsgNetworkConnected&) {
            const auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->context_) {
                return;
            }
            self->transport_connected_ = true;
            self->context_->PostTask([weak_self]() {
                if (const auto self = weak_self.lock(); self && !self->exiting_) {
                    self->ReportTransportConnected();
                }
            });
        });
        msg_listener_->Listen<SdkMsgNetworkDisConnected>(
            [weak_self](const SdkMsgNetworkDisConnected&) {
                if (const auto self = weak_self.lock(); self && !self->exiting_) {
                    self->transport_connected_ = false;
                    self->transport_reported_ = false;
                }
            });
        msg_listener_->Listen<SdkMsgWsConnectionRejected>(
            [weak_self](const SdkMsgWsConnectionRejected& event) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_ || !self->context_) {
                    return;
                }
                self->transport_rejection_.store(
                    static_cast<int>(event.rejection_), std::memory_order_release);
                self->context_->PostTask([weak_self]() {
                    if (const auto self = weak_self.lock(); self && !self->exiting_) {
                        self->ReportTransportRejected();
                    }
                });
            });
        client_ = std::make_shared<asio2::ws_client>();
        const auto runtime = context_->GetMessageNotifier()->GetAsyncRuntime();
        connection_scope_ = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
        reconnect_supervisor_ = PxReconnectSupervisor::Create(runtime, MakeWebSocketReconnectOptions("client_panel"));
        if (!connection_scope_ || !reconnect_supervisor_) {
            LOGE("event=module.start component=client_panel code=ASYNC_WORKFLOW_CREATE_FAILED "
                 "operation=start_client outcome=failed recoverable=false");
            Exit();
            return;
        }
        client_->set_auto_reconnect(false);
        client_->keep_alive(true);
        client_->set_timeout(std::chrono::milliseconds(3000));

        client_->bind_init([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_) {
                return;
            }
            self->transport_reported_ = false;
            self->client_->ws_stream().binary(true);
            self->client_->set_no_delay(true);
        }).bind_connect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
                static_cast<void>(self->reconnect_supervisor_->FailActive(MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected,
                    "panel.connect",
                    asio2::last_error_msg(),
                    true)));
            } else {
                LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
            }
        }).bind_upgrade([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
                static_cast<void>(self->reconnect_supervisor_->FailActive(MakePxAsyncError(
                    PxAsyncErrorCode::kProtocolError,
                    "panel.upgrade",
                    asio2::last_error_msg(),
                    true)));
                return;
            }
            static_cast<void>(self->reconnect_supervisor_->MarkReady());
        }).bind_disconnect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            self->transport_reported_ = false;
            LOGE("CtPanelClient disconnected");
            static_cast<void>(self->reconnect_supervisor_->MarkDisconnected(MakePxAsyncError(
                PxAsyncErrorCode::kServiceNotConnected,
                "panel.disconnect",
                "Panel websocket disconnected",
                true)));
        }).bind_recv([weak_self](std::string_view data) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            auto msg = std::string(data.data(), data.size());
            self->ParseMessage(msg);
        });

        const auto& settings = *Settings::Instance();
        auto path = std::format("/panel?stream_id={}", settings.stream_id_);
        PxReconnectSupervisorHooks hooks{
            .start_attempt = [client = client_, port = settings.panel_server_port_, path = std::move(path)](std::uint64_t) {
                return StartWebSocketAdapter(client, "127.0.0.1", port, path, "client-panel.start");
            },
            .stop_attempt = [client = client_](const std::chrono::steady_clock::time_point deadline) {
                return StopWebSocketAdapter(client, deadline, "client-panel.retry-reset");
            },
            .on_ready = [weak_self](std::uint64_t) {
                if (const auto self = weak_self.lock(); self && !self->exiting_) {
                    self->Hello();
                }
            },
        };
        if (!connection_scope_->Spawn("client-panel-reconnect", [supervisor = reconnect_supervisor_, hooks = std::move(hooks)]() mutable {
                return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
            })
            || !connection_scope_->Spawn("client-panel-heartbeat", [weak_self] {
                return RunHeartbeatLoop(weak_self);
            })) {
            LOGE("event=module.start component=client_panel code=ASYNC_SCOPE_SPAWN_FAILED "
                 "operation=start_workflows outcome=failed recoverable=false");
            Exit();
        }
    }

    PxAwaitable<void> CtPanelClient::RunHeartbeatLoop(std::weak_ptr<CtPanelClient> weak_client) {
        for (;;) {
            const auto waited = co_await WaitForAsyncDelay(std::chrono::seconds(1), "client-panel.heartbeat");
            const auto self = weak_client.lock();
            if (!waited || !self || self->exiting_) {
                co_return;
            }
            self->HeartBeat();
        }
    }

    void CtPanelClient::Exit() {
        exiting_ = true;
        transport_connected_ = false;
        transport_reported_ = false;
        transport_rejection_ = 0;
        msg_listener_ = nullptr;
        if (reconnect_supervisor_) {
            reconnect_supervisor_->Stop();
        }
        if (client_) {
            static_cast<void>(RequestAsioClientStop(client_, "client-panel.stop"));
        }
        if (connection_scope_) {
            connection_scope_->BeginStop();
            if (connection_scope_->IsScopeThread()) {
                return;
            }
            static_cast<void>(connection_scope_->WaitFor(std::chrono::seconds(5)));
        }
        static_cast<void>(RequestAsioClientStop(client_, "client-panel.stop-confirm"));
        static_cast<void>(WaitForAsioClientStoppedBlocking(
            client_, std::chrono::steady_clock::now() + std::chrono::seconds(3)));
        client_.reset();
        reconnect_supervisor_.reset();
        connection_scope_.reset();
    }

    bool CtPanelClient::IsAlive() {
        return !exiting_ && client_ && client_->is_started()
            && reconnect_supervisor_ && reconnect_supervisor_->IsReady();
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
        const auto& settings = *Settings::Instance();

        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpHello);
        cp_msg.set_stream_id(settings.stream_id_);
        auto& sub = *cp_msg.mutable_hello();
#ifdef WIN32
        sub.set_type(pxcp::CpSessionType::kWindowsClient);
#endif
        client->async_send(cp_msg.SerializeAsString());
        ReportTransportConnected();
        ReportTransportRejected();
    }

    void CtPanelClient::ReportTransportConnected() {
        if (!transport_connected_ || !IsAlive()) {
            return;
        }
        bool expected = false;
        if (!transport_reported_.compare_exchange_strong(expected, true)) {
            return;
        }
        const auto client = client_;
        if (!client) {
            transport_reported_ = false;
            return;
        }
        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpTransportConnected);
        const auto& settings = *Settings::Instance();
        cp_msg.set_stream_id(settings.stream_id_);
        client->async_send(cp_msg.SerializeAsString());
        LOGI("Reported remote transport connected to Panel");
    }

    void CtPanelClient::ReportTransportRejected() {
        if (!IsAlive()) {
            return;
        }
        const auto rejection = static_cast<WsControlRejection>(
            transport_rejection_.load(std::memory_order_acquire));
        if (rejection == WsControlRejection::kNone) {
            return;
        }
        const auto client = client_;
        if (!client) {
            return;
        }
        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpTransportRejected);
        const auto& settings = *Settings::Instance();
        cp_msg.set_stream_id(settings.stream_id_);
        auto& rejected = *cp_msg.mutable_transport_rejected();
        if (rejection == WsControlRejection::kAuthorization) {
            rejected.set_reason(pxcp::CpTransportRejection::kCpRejectionAuthorization);
        }
        else if (rejection == WsControlRejection::kOccupied) {
            rejected.set_reason(pxcp::CpTransportRejection::kCpRejectionOccupied);
        }
        else {
            rejected.set_reason(pxcp::CpTransportRejection::kCpRejectionSessionPolicy);
        }
        client->async_send(cp_msg.SerializeAsString());
        transport_rejection_.store(0, std::memory_order_release);
        LOGI("Reported terminal remote transport rejection to Panel: {}",
             static_cast<int>(rejection));
    }

    void CtPanelClient::HeartBeat() {
        if (!IsAlive()) {
            return;
        }
        auto client = client_;
        if (!client) {
            return;
        }

        const auto& stat = *px::SdkStatistics::Instance();
        const auto& settings = *Settings::Instance();

        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpHeartBeat);
        cp_msg.set_stream_id(settings.stream_id_);
        auto& sub = *cp_msg.mutable_heartbeat();
        sub.set_remote_device_desktop_name(stat.remote_desktop_name_.Clone());
        sub.set_remote_os_name(stat.remote_os_name_.Clone());
        client->async_send(cp_msg.SerializeAsString());
    }

    void CtPanelClient::RequestRtcIceRestart() {
        if (!IsAlive() || !client_) {
            LOGW("Cannot request RTC ICE restart: Panel channel is offline");
            return;
        }
        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpRtcIceRestartRequest);
        const auto& settings = *Settings::Instance();
        cp_msg.set_stream_id(settings.stream_id_);
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
        const auto& settings = *Settings::Instance();
        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpFileTransferBegin);
        cp_msg.set_stream_id(settings.stream_id_);
        auto& sub = *cp_msg.mutable_ft_transfer_beg();
        sub.set_the_file_id(msg.the_file_id_);
        sub.set_begin_timestamp(msg.begin_timestamp_);
        sub.set_direction(msg.direction_);
        sub.set_file_detail(msg.file_detail_);
        sub.set_remote_device_id(msg.remote_device_id_);
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
        const auto& settings = *Settings::Instance();
        pxcp::CpMessage cp_msg;
        cp_msg.set_type(pxcp::CpMessageType::kCpFileTransferEnd);
        cp_msg.set_stream_id(settings.stream_id_);
        auto& terminal = *cp_msg.mutable_ft_transfer_end();
        terminal.set_the_file_id(msg.the_file_id_);
        terminal.set_end_timestamp(msg.end_timestamp_);
        terminal.set_success(msg.success_);
        terminal.set_status(msg.status_);
        terminal.set_end_reason(msg.end_reason_);
        client->async_send(cp_msg.SerializeAsString());
    }

}
