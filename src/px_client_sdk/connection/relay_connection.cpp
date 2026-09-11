#include "relay_connection.h"

#include "px_common/data.h"
#include "px_common/log.h"
#include "px_common/message_notifier.h"
#include "px_relay_client/relay_client_sdk.h"
#include "px_relay_client/relay_net_client.h"
#include "relay_message.pb.h"

#include <utility>

namespace px {

RelayConnection::RelayConnection(SdkConnectionParams params, const std::shared_ptr<MessageNotifier>& notifier)
    : Connection(notifier), params_(std::move(params)), relay_sdk_(std::make_shared<RelayClientSdk>(
                                                            RelayClientSdkParam{
                                                                .host_ = params_.relay_host_,
                                                                .port_ = params_.relay_port_,
                                                                .device_id_ = params_.relay_device_id_,
                                                                .remote_device_id_ = params_.relay_remote_device_id_,
                                                                .stream_id_ = params_.stream_id_,
                                                                .device_name_ = params_.device_name_,
                                                                .appkey_ = params_.appkey_,
                                                                .force_gdi_ = params_.force_gdi_,
                                                                .connection_ticket_ = params_.connection_ticket_,
                                                                .connection_nonce_ = params_.connection_nonce_,
                                                                .connection_ticket_device_id_ = params_.relay_ticket_device_id_,
                                                                .connection_instance_id_ = params_.connection_instance_id_,
                                                                .ticket_scope_ = RelayTicketScope::kMedia,
                                                            },
                                                            notifier->GetAsyncRuntime())) {}

RelayConnection::~RelayConnection() {
    Stop();
}

void RelayConnection::Start() {
    if (!relay_sdk_ || stopped_.load(std::memory_order_acquire) || started_.exchange(true, std::memory_order_acq_rel))
        return;
    const auto weak_self = weak_from_this();
    relay_sdk_->SetOnRelayProtoMessageCallback([weak_self](const std::shared_ptr<px_relay::RelayMessage>& message) {
        const auto self = weak_self.lock();
        if (!self || self->stopped_.load(std::memory_order_acquire) || message->type() != px_relay::RelayMessageType::kRelayTargetMessage)
            return;
        if (self->msg_cbk_)
            self->msg_cbk_(Data::From(message->relay().payload()));
    });
    relay_sdk_->SetOnRelayServerConnectedCallback([weak_self]() {
        if (const auto self = weak_self.lock())
            LOGI("Relay transport connected; waiting for the target room.");
    });
    relay_sdk_->SetOnRelayServerDisConnectedCallback([weak_self]() {
        const auto self = weak_self.lock();
        if (!self || self->stopped_.load(std::memory_order_acquire))
            return;
        self->room_ready_.store(false, std::memory_order_release);
        self->NotifyFileTransferClosed();
        if (self->dis_conn_cbk_)
            self->dis_conn_cbk_();
    });
    relay_sdk_->SetOnRelayRoomPreparedCallback([weak_self](const std::shared_ptr<px_relay::RelayMessage>&) {
        const auto self = weak_self.lock();
        if (!self || self->stopped_.load(std::memory_order_acquire))
            return;
        const bool first_ready = !self->room_ready_.exchange(true, std::memory_order_acq_rel);
        self->RequestResumeStream();
        self->NotifyFileTransferWritable();
        if (first_ready && self->conn_cbk_)
            self->conn_cbk_();
    });
    relay_sdk_->SetOnRelayRoomDestroyedCallback([weak_self](const std::shared_ptr<px_relay::RelayMessage>&) {
        if (const auto self = weak_self.lock())
            self->room_ready_.store(false, std::memory_order_release);
    });
    relay_sdk_->SetOnRelayErrorCallback([](const std::shared_ptr<px_relay::RelayMessage>& message) {
        const auto& error = message->relay_error();
        LOGE("Relay request failed: code={}, operation={}", static_cast<int>(error.code()), static_cast<int>(error.which_message()));
    });
    relay_sdk_->SetOnRelayRemoteDeviceOffline([weak_self](const std::shared_ptr<px_relay::RelayMessage>&) {
        if (const auto self = weak_self.lock())
            self->room_ready_.store(false, std::memory_order_release);
    });
    relay_sdk_->Start();
}

void RelayConnection::Stop() {
    if (stopped_.exchange(true, std::memory_order_acq_rel))
        return;
    room_ready_.store(false, std::memory_order_release);
    NotifyFileTransferClosed();
    if (relay_sdk_)
        relay_sdk_->Stop();
    Connection::Stop();
}

void RelayConnection::PostBinaryMessage(std::shared_ptr<Data> message) {
    if (relay_sdk_ && room_ready_.load(std::memory_order_acquire))
        relay_sdk_->RelayProtoMessage(std::move(message));
}

void RelayConnection::PostReliableBinaryMessage(std::shared_ptr<Data> message, std::function<void(bool)> completion) {
    if (!message || !IsAlive()) {
        if (completion) completion(false);
        return;
    }
    relay_sdk_->RelayProtoMessageReliable(std::move(message), std::move(completion));
}

int64_t RelayConnection::GetQueuingMsgCount() {
    return relay_sdk_ ? relay_sdk_->GetQueuingMsgCount() : 0;
}

void RelayConnection::RequestPauseStream() {
    if (relay_sdk_ && room_ready_.load(std::memory_order_acquire))
        relay_sdk_->RequestPauseStream();
}

void RelayConnection::RequestResumeStream() {
    if (relay_sdk_)
        relay_sdk_->RequestResumeStream();
}

void RelayConnection::On16msTimeout() {
    if (GetQueuingMsgCount() <= kFileTransferQueueLowWatermark)
        NotifyFileTransferWritable();
}

bool RelayConnection::IsAlive() {
    const auto net_client = relay_sdk_ ? relay_sdk_->GetNetClient() : std::shared_ptr<RelayNetClient>{};
    return room_ready_.load(std::memory_order_acquire) && net_client && net_client->IsAlive();
}

std::shared_ptr<FileTransferWritableSignal> RelayConnection::AcquireFileTransferWritableSignal() {
    return relay_sdk_ ? relay_sdk_->AcquireFileTransferWritableSignal() : Connection::AcquireFileTransferWritableSignal();
}

} // namespace px
