//
// Created by RGAA on 28/02/2025.
//

#include "relay_client_sdk.h"
#include "relay_ws_client.h"
#include "relay_message.pb.h"
#include "relay_room.h"
#include "px_common_new/data.h"

#include <utility>

using namespace px_relay;

namespace px
{

    RelayClientSdk::RelayClientSdk(const RelayClientSdkParam& param)
        : RelayClientSdk(
            param,
            std::make_shared<RelayWsClient>(
                param.host_, param.port_, param.device_id_, param.device_name_,
                param.stream_id_, param.appkey_, param.force_gdi_,
                param.remote_device_id_, param.connection_ticket_,
                param.connection_nonce_)) {}

    RelayClientSdk::RelayClientSdk(
        const RelayClientSdkParam& param,
        std::shared_ptr<RelayNetClient> net_client)
        : sdk_param_(param), ws_client_(std::move(net_client)) {}

    void RelayClientSdk::SetOnRelayServerConnectedCallback(OnRelayServerConnected&& cbk) {
        const auto weak_self = weak_from_this();
        ws_client_->SetOnRelayServerConnectedCallback(
            [weak_self, callback = std::move(cbk)]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            callback();
            LOGI("connected to relay server, will create room.");
            self->RequestCreateRoom();
        });
    }

    void RelayClientSdk::SetOnRelayServerDisConnectedCallback(OnRelayServerDisConnected&& cbk) {
        const auto weak_self = weak_from_this();
        ws_client_->SetOnRelayServerDisConnectedCallback(
            [weak_self, callback = std::move(cbk)]() {
            if (!weak_self.lock()) {
                return;
            }
            callback();
        });
    }

    void RelayClientSdk::SetOnRelayRoomPreparedCallback(OnRelayRoomPrepared&& cbk) {
        cbk_room_prepared_ = cbk;
    }

    void RelayClientSdk::SetOnRelayRoomDestroyedCallback(OnRelayRoomDestroyed&& cbk) {
        cbk_room_destroyed_ = cbk;
    }

    void RelayClientSdk::SetOnRelayErrorCallback(OnRelayError&& cbk) {
        cbk_relay_error_ = cbk;
    }

    void RelayClientSdk::SetOnRelayRemoteDeviceOffline(OnRelayRemoteDeviceOffline&& cbk) {
        cbk_remote_device_offline_ = cbk;
    }

    void RelayClientSdk::SetOnRelayProtoMessageCallback(std::function<void(const std::shared_ptr<RelayMessage>&)>&& cbk) {
        const auto weak_self = weak_from_this();
        ws_client_->SetOnRelayProtoMessageCallback(
            [weak_self, callback = std::move(cbk)](std::shared_ptr<Data> msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            auto rl_msg = self->ProcessProtoMessage(std::move(msg));
            if (rl_msg) {
                callback(rl_msg);
            }
            else {
                LOGE("Parse relay proto message failed!");
                return;
            }

            auto type = rl_msg->type();
            if (type == RelayMessageType::kRelayCreateRoomResp) {
                self->OnCreatedRoomResp(rl_msg);
                LOGI("will request control.");
                self->RequestControl();
            }
            else if (type == RelayMessageType::kRelayRequestControlResp) {
                self->OnRequestControlResp(rl_msg);
            }
            else if (type == RelayMessageType::kRelayRequestStopResp) {
                self->OnRequestStopRelayResp(rl_msg);
            }
            else if (type == RelayMessageType::kRelayError) {
                self->OnErrorMessage(rl_msg);
            }
            else if (type == RelayMessageType::kRelayRoomPrepared) {
                self->OnRoomPrepared(rl_msg);
            }
            else if (type == RelayMessageType::kRelayRemoteDeviceOffline) {
                self->OnRemoteDeviceOffline(rl_msg);
            }
        });
    }

    void RelayClientSdk::Start() {
        ws_client_->Start();
    }

    void RelayClientSdk::Stop() {
        if (ws_client_) {
            ws_client_->Stop();
        }
    }

    std::shared_ptr<RelayNetClient> RelayClientSdk::GetNetClient() {
        return ws_client_;
    }

    int64_t RelayClientSdk::GetQueuingMsgCount() {
        return ws_client_->GetQueuingMsgCount();
    }

    std::shared_ptr<FileTransferWritableSignal>
    RelayClientSdk::AcquireFileTransferWritableSignal() {
        return ws_client_ ? ws_client_->AcquireFileTransferWritableSignal()
                          : std::shared_ptr<FileTransferWritableSignal>{};
    }

    void RelayClientSdk::RelayProtoMessage(std::shared_ptr<Data> msg) {
        std::string room_id;
        std::shared_ptr<RelayNetClient> net_client;
        {
            std::lock_guard<std::mutex> lk(relay_mtx_);
            if (!room_ || !room_->IsValid() || !ws_client_) {
                //LOGE("Can't relay message, room is null.");
                return;
            }
            room_id = room_->room_id_;
            net_client = ws_client_;
        }

        const auto weak_self = weak_from_this();
        net_client->PostNetTask(
            [weak_self, msg = std::move(msg), room_id = std::move(room_id)]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            // msg : px::Message
            // rl_msg : px::RelayMessage
            RelayMessage rl_msg;
            rl_msg.set_from_device_id(self->sdk_param_.device_id_);
            rl_msg.set_type(RelayMessageType::kRelayTargetMessage);
            auto relay = rl_msg.mutable_relay();
            relay->set_relay_msg_index(self->relay_msg_index_++);
            auto room_ids = relay->mutable_room_ids();
            room_ids->Add(room_id.c_str());
            relay->set_payload(msg->AsString());

            self->PostBinMessage(rl_msg.SerializeAsString());
            //LOGI("Relay from: {} to room: {}, relay index: {}", sdk_param_.device_id_, room_->room_id_, relay_msg_index_);
        });
    }

    void RelayClientSdk::PostBinMessage(const std::string& msg) {
        if (ws_client_) {
            ws_client_->PostBinaryMessage(msg);
        }
    }

    std::shared_ptr<RelayMessage> RelayClientSdk::ProcessProtoMessage(std::shared_ptr<Data> msg) {
        auto rl_msg = std::make_shared<RelayMessage>();
        if (!rl_msg->ParsePartialFromArray(msg->CStr(), msg->Size())) {
            return nullptr;
        }
        return rl_msg;
    }

    // send from client
    void RelayClientSdk::RequestCreateRoom() {
        RelayMessage rl_msg;
        rl_msg.set_type(RelayMessageType::kRelayCreateRoom);
        auto sub = rl_msg.mutable_create_room();
        sub->set_device_id(sdk_param_.device_id_);
        sub->set_remote_device_id(sdk_param_.remote_device_id_);
        sub->set_device_name(sdk_param_.device_name_);
        sub->set_stream_id(sdk_param_.stream_id_);
        this->PostBinMessage(rl_msg.SerializeAsString());
        LOGI("create room sent, device id: {}, remote device id: {}", sdk_param_.device_id_, sdk_param_.remote_device_id_);
    }

    // received from server
    void RelayClientSdk::OnCreatedRoomResp(const std::shared_ptr<RelayMessage>& msg) {
        auto cr = msg->create_room_resp();
        if (!room_) {
            room_ = std::make_shared<RelayRoom>();
        }
        room_->room_id_ = cr.room_id();
        room_->device_id_ = cr.device_id();
        room_->remote_device_id_ = cr.remote_device_id();
        LOGI("on create room response, device id: {}, remote device id: {}, room id: {}", sdk_param_.device_id_, sdk_param_.remote_device_id_, room_->room_id_);
    }

    // send from client
    void RelayClientSdk::RequestControl() {
        if (!room_ || !room_->IsValid()) {
            LOGE("Can't request control, room is not valid!");
            return;
        }
        RelayMessage rl_msg;
        rl_msg.set_type(RelayMessageType::kRelayRequestControl);
        auto sub = rl_msg.mutable_request_control();
        sub->set_device_id(sdk_param_.device_id_);
        sub->set_remote_device_id(sdk_param_.remote_device_id_);
        sub->set_room_id(room_->room_id_);
        sub->set_device_name(sdk_param_.device_name_);
        sub->set_stream_id(sdk_param_.stream_id_);
        sub->set_force_gdi(sdk_param_.force_gdi_);
        this->PostBinMessage(rl_msg.SerializeAsString());
        LOGI("Request control: {}", room_->room_id_);
    }

    // received from server
    void RelayClientSdk::OnRequestControlResp(const std::shared_ptr<RelayMessage>& msg) {
        LOGI("On request control resp!!");
    }

    // send from client
    void RelayClientSdk::RequestStopRelay() {
        if (!room_) {
            LOGE("Can't request stop, room is null.");
            return;
        }
        RelayMessage rl_msg;
        rl_msg.set_type(RelayMessageType::kRelayRequestStop);
        auto sub = rl_msg.mutable_request_stop();
        sub->set_device_id(sdk_param_.device_id_);
        sub->set_room_id(room_->room_id_);
        sub->set_remote_device_id(sdk_param_.remote_device_id_);
        this->PostBinMessage(rl_msg.SerializeAsString());
    }

    // received from server
    void RelayClientSdk::OnRequestStopRelayResp(const std::shared_ptr<RelayMessage>& msg) {
        // todo: callback it.
    }

    void RelayClientSdk::OnErrorMessage(const std::shared_ptr<RelayMessage>& msg) {
        auto re = msg->relay_error();
        LOGI("OnErrorMessage, code: {}, msg: {}, which: {}", (int)re.code(), re.message(), (int)re.which_message());
        if (cbk_relay_error_) {
            cbk_relay_error_(msg);
        }
    }

    void RelayClientSdk::OnRoomPrepared(const std::shared_ptr<RelayMessage>& msg) {
        auto in_room_id = msg->room_prepared().room_id();
        LOGI("Room prepared, room id: {}", in_room_id);
        if (!room_ || room_->room_id_ != in_room_id) {
            LOGE("Can't process RoomPrepared, room is null! {}", in_room_id);
            return;
        }
        auto rp = msg->room_prepared();
        room_->device_id_ = rp.device_id();
        room_->remote_device_id_ = rp.remote_device_id();
        room_->creator_device_id_ = rp.creator_device_id();
        room_->creator_device_name_ = rp.creator_device_name();
        room_->creator_stream_id_ = rp.creator_stream_id();
        LOGI("Room prepared, device id: {}, device id: {}, device name: {}, stream id: {}", room_->device_id_, room_->remote_device_id_,
             room_->creator_device_name_, room_->creator_stream_id_);

        if (cbk_room_prepared_) {
            cbk_room_prepared_(msg);
        }
    }

    void RelayClientSdk::OnRoomInfoChanged(const std::shared_ptr<RelayMessage>& msg) {
        auto rc = msg->room_info_changed();
        const auto& room_id = rc.room_id();

    }

    void RelayClientSdk::OnRoomDestroyed(const std::shared_ptr<RelayMessage>& msg) {
        auto rd = msg->room_destroyed();
        room_->Clear();

        if (cbk_room_destroyed_) {
            cbk_room_destroyed_(msg);
        }
    }

    void RelayClientSdk::OnRemoteDeviceOffline(const std::shared_ptr<px_relay::RelayMessage>& msg) {
        if (cbk_remote_device_offline_) {
            cbk_remote_device_offline_(msg);
        }
    }

    bool RelayClientSdk::IsInRoom() {
        return room_ && room_->IsValid();
    }

    // request pause stream
    void RelayClientSdk::RequestPauseStream() {
        if (!room_) {
            LOGE("Can't request pause stream, room is null.");
            return;
        }
        RelayMessage rl_msg;
        rl_msg.set_from_device_id(sdk_param_.device_id_);
        rl_msg.set_type(RelayMessageType::kRelayRequestPausedStream);
        auto sub = rl_msg.mutable_request_pause();
        sub->set_device_id(sdk_param_.device_id_);
        sub->set_room_id(room_->room_id_);
        sub->set_remote_device_id(sdk_param_.remote_device_id_);
        this->PostBinMessage(rl_msg.SerializeAsString());
        LOGI("Request pause stream: {}", room_->room_id_);
    }

    // request resume stream
    void RelayClientSdk::RequestResumeStream() {
        if (!room_) {
            LOGE("Can't request resume stream, room is null.");
            return;
        }
        RelayMessage rl_msg;
        rl_msg.set_from_device_id(sdk_param_.device_id_);
        rl_msg.set_type(RelayMessageType::kRelayRequestResumeStream);
        auto sub = rl_msg.mutable_request_resume();
        sub->set_device_id(sdk_param_.device_id_);
        sub->set_room_id(room_->room_id_);
        sub->set_remote_device_id(sdk_param_.remote_device_id_);
        this->PostBinMessage(rl_msg.SerializeAsString());
        LOGI("Request resume stream: {}", room_->room_id_);
    }

    void RelayClientSdk::RetryConnection() {
        if (ws_client_ && ws_client_->IsAlive()) {
            this->RequestCreateRoom();
        }
    }

}
