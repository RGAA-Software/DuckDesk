//
// Created by RGAA on 1/03/2025.
//

#include "relay_connection.h"
#include "relay_message.pb.h"
#include "tc_common_new/data.h"
#include "tc_relay_client/relay_client_sdk.h"
#include "tc_relay_client/relay_net_client.h"
#include "tc_common_new/message_notifier.h"
#include "tc_client_sdk_new/sdk_messages.h"

using namespace relay;

namespace tc
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
        });

        relay_sdk_->SetOnRelayProtoMessageCallback([=, this](const std::shared_ptr<RelayMessage>& rl_msg) {
            if (rl_msg->type() == RelayMessageType::kRelayTargetMessage) {
                auto sub = rl_msg->relay();
                auto relay_msg_index = sub.relay_msg_index();
                if (msg_cbk_) {
                    auto payload = Data::From(sub.payload());
                    msg_cbk_(payload);
                }
            }
        });

        relay_sdk_->SetOnRelayServerConnectedCallback([=, this]() {
            LOGI("Relay server connected.");
            if (conn_cbk_) {
                conn_cbk_();
            }
        });

        relay_sdk_->SetOnRelayServerDisConnectedCallback([=, this]() {
            LOGI("Relay server disconnected.");
            if (dis_conn_cbk_) {
                dis_conn_cbk_();
            }
        });

        relay_sdk_->SetOnRelayRoomPreparedCallback([=, this](const std::shared_ptr<relay::RelayMessage>& msg) {
            LOGI("Auto relay: {}", auto_relay_);
            if (auto_relay_) {
                this->RequestResumeStream();
            }

            // notify
            msg_notifier_->SendAppMessage(SdkMsgRoomPrepared{
                .room_type_ = room_type,
            });
        });

        relay_sdk_->SetOnRelayRoomDestroyedCallback([=, this](const std::shared_ptr<relay::RelayMessage>& msg) {
            // notify
            msg_notifier_->SendAppMessage(SdkMsgRoomDestroyed{});
        });

        // error
        relay_sdk_->SetOnRelayErrorCallback([=, this](const std::shared_ptr<relay::RelayMessage>& msg) {
            auto re = msg->relay_error();
            msg_notifier_->SendAppMessage(SdkMsgRelayError {
                .code_ = re.code(),
                .msg_ = re.message(),
                .which_msg_ = re.which_message(),
            });
        });

        // remote device offline
        relay_sdk_->SetOnRelayRemoteDeviceOffline([=, this](const std::shared_ptr<relay::RelayMessage>& msg) {
            auto sub = msg->remote_device_offline();
            msg_notifier_->SendAppMessage(SdkMsgRelayRemoteDeviceOffline {
                .device_id_ = sub.device_id(),
                .remote_device_id_ = sub.remote_device_id(),
                .room_id_ = sub.room_id(),
            });
        });
    }

    RelayConnection::~RelayConnection() {

    }

    void RelayConnection::Start() {
        relay_sdk_->Start();
    }

    void RelayConnection::Stop() {
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

}
