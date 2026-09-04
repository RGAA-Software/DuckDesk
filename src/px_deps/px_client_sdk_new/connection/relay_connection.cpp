//
// Created by RGAA on 1/03/2025.
//

#include "relay_connection.h"
#include "relay_message.pb.h"
#include "px_common_new/data.h"
#include "px_relay_client/relay_client_sdk.h"
#include "px_relay_client/relay_net_client.h"
#include "px_common_new/message_notifier.h"
#include "px_client_sdk_new/sdk_messages.h"

using namespace px_relay;

namespace px
{

    RelayConnection::RelayConnection(const std::shared_ptr<ThunderSdkParams>& params,
                                     const std::shared_ptr<MessageNotifier>& notifier,
                                     const std::string& host,
                                     int port,
                                     const std::string& device_id,
                                     const std::string& remote_device_id,
                                     bool auto_relay,
                                     const std::string& room_type) : Connection(params, notifier) {
        this->host_ = host;
        this->port_ = port;
        this->device_id_ = device_id;
        this->remote_device_id_ = remote_device_id;
        this->auto_relay_ = auto_relay;
        this->room_type_ = room_type;
        relay_sdk_ = std::make_shared<RelayClientSdk>(RelayClientSdkParam {
            .host_ = host,
            .port_ = port,
            .device_id_ = device_id,
            .remote_device_id_ = remote_device_id,
            .stream_id_ = params->stream_id_,
            .device_name_ = params->device_name_,
            .appkey_ = params->appkey_,
            .force_gdi_ = params->force_gdi_,
            .connection_ticket_ = room_type == kRoomTypeFileTransfer ? params->connection_ticket_ : "",
            .connection_nonce_ = room_type == kRoomTypeFileTransfer ? params->connection_nonce_ : "",
        }, notifier->GetAsyncRuntime());

    }

    RelayConnection::~RelayConnection() {
        Stop();
    }

    void RelayConnection::Start() {
        const auto weak_self = weak_from_this();
        relay_sdk_->SetOnRelayProtoMessageCallback([weak_self](const std::shared_ptr<RelayMessage>& rl_msg) {
            const auto self = weak_self.lock();
            if (!self || rl_msg->type() != RelayMessageType::kRelayTargetMessage) return;
            if (self->msg_cbk_) {
                self->msg_cbk_(Data::From(rl_msg->relay().payload()));
            }
        });
        relay_sdk_->SetOnRelayServerConnectedCallback([weak_self]() {
            LOGI("Relay server connected.");
            if (const auto self = weak_self.lock()) {
                self->NotifyFileTransferWritable();
                if (self->conn_cbk_) self->conn_cbk_();
            }
        });
        relay_sdk_->SetOnRelayServerDisConnectedCallback([weak_self]() {
            LOGI("Relay server disconnected.");
            if (const auto self = weak_self.lock()) {
                self->NotifyFileTransferClosed();
                if (self->dis_conn_cbk_) self->dis_conn_cbk_();
            }
        });
        relay_sdk_->SetOnRelayRoomPreparedCallback(
            [weak_self](const std::shared_ptr<px_relay::RelayMessage>&) {
                const auto self = weak_self.lock();
                if (!self) return;
                LOGI("Auto relay: {}", self->auto_relay_);
                if (self->auto_relay_) self->RequestResumeStream();
                self->msg_notifier_->SendAppMessage(SdkMsgRoomPrepared{.room_type_ = self->room_type_});
            });
        relay_sdk_->SetOnRelayRoomDestroyedCallback(
            [weak_self](const std::shared_ptr<px_relay::RelayMessage>&) {
                if (const auto self = weak_self.lock()) {
                    self->msg_notifier_->SendAppMessage(SdkMsgRoomDestroyed{});
                }
            });
        relay_sdk_->SetOnRelayErrorCallback(
            [weak_self](const std::shared_ptr<px_relay::RelayMessage>& msg) {
                if (const auto self = weak_self.lock()) {
                    const auto& error = msg->relay_error();
                    self->msg_notifier_->SendAppMessage(SdkMsgRelayError {
                        .code_ = error.code(), .msg_ = error.message(),
                        .which_msg_ = error.which_message(),
                    });
                }
            });
        relay_sdk_->SetOnRelayRemoteDeviceOffline(
            [weak_self](const std::shared_ptr<px_relay::RelayMessage>& msg) {
                if (const auto self = weak_self.lock()) {
                    const auto& offline = msg->remote_device_offline();
                    self->msg_notifier_->SendAppMessage(SdkMsgRelayRemoteDeviceOffline {
                        .device_id_ = offline.device_id(),
                        .remote_device_id_ = offline.remote_device_id(),
                        .room_id_ = offline.room_id(),
                    });
                }
            });
        relay_sdk_->Start();
    }

    void RelayConnection::Stop() {
        Connection::Stop();
        if (relay_sdk_) {
            relay_sdk_->Stop();
        }
    }

    void RelayConnection::PostBinaryMessage(std::shared_ptr<Data> msg) {
        if (relay_sdk_) {
            relay_sdk_->RelayProtoMessage(msg);
        }
    }

    int64_t RelayConnection::GetQueuingMsgCount() {
        if (relay_sdk_) {
            return relay_sdk_->GetQueuingMsgCount();
        }
        return Connection::GetQueuingMsgCount();
    }

    void RelayConnection::RequestPauseStream() {
        if (relay_sdk_) {
            relay_sdk_->RequestPauseStream();
        }
    }

    void RelayConnection::RequestResumeStream() {
        if (relay_sdk_) {
            relay_sdk_->RequestResumeStream();
        }
    }

    void RelayConnection::RetryConnection() {
        if (relay_sdk_) {
            relay_sdk_->RetryConnection();
        }
    }

    void RelayConnection::On16msTimeout() {
        if (!relay_sdk_) {
            NotifyFileTransferClosed();
            return;
        }
        if (relay_sdk_->GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) {
            NotifyFileTransferWritable();
        }
    }

    std::shared_ptr<FileTransferWritableSignal>
    RelayConnection::AcquireFileTransferWritableSignal() {
        return relay_sdk_ ? relay_sdk_->AcquireFileTransferWritableSignal()
                          : Connection::AcquireFileTransferWritableSignal();
    }

}
