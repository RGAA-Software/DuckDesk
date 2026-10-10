#include "connection/iroh_connection.h"
#include "connection/iroh_dialer.h"
#include "px_common/data.h"
#include "px_common/log.h"
#include "sdk_net_client.h"
#include "sdk_statistics.h"

namespace px {
bool NetClient::PostIrohVoice(const Message& message) {
    const auto connection = std::dynamic_pointer_cast<IrohConnection>(CurrentMediaConnection());
    return connection && connection->IsAlive() && connection->SendVoice(message);
}

void NetClient::StartIrohConnection() {
    const auto owner = weak_from_this();
    const auto dialer = std::make_shared<IrohDialer>(*params_.iroh_, [owner](IrohDialResult result) {
        const auto client = owner.lock();
        if (!client || client->exited_) return;
        if (result.stage == IrohDialStage::kStarting) {
            client->managed_media_generation_.fetch_add(1);
            std::shared_ptr<Connection> previous{};
            {
                std::lock_guard lock(client->media_connection_mutex_);
                previous = std::exchange(client->media_conn_, {});
            }
            if (previous) previous->Stop();
            const bool was_connected = client->connection_notified_.exchange(false);
            // Reset decode state after draining old media callbacks as well as at
            // the initial disconnect, so queued old frames cannot enter the new session.
            if (!client->exited_ && (previous || was_connected) && client->dis_conn_cbk_) client->dis_conn_cbk_();
            return;
        }
        if (!result.connection) {
            LOGW("event=iroh.connect outcome=failed code={}", result.error_code);
            if (client->dis_conn_cbk_) client->dis_conn_cbk_();
            return;
        }
        const auto generation = client->managed_media_generation_.load();
        const auto adapter = std::make_shared<IrohConnection>(client->msg_notifier_, result.connection, std::move(result.channels));
        if (client->params_.session_mode_ == SdkSessionMode::kNative && !client->params_.file_transfer_only_) {
            adapter->SetVoiceCallback([owner, generation](std::shared_ptr<Message> message) {
                const auto client = owner.lock();
                if (!client || !client->IsCurrentManagedMediaConnection(generation)) return;
                message->set_device_id(client->params_.device_id_);
                message->set_stream_id(client->params_.stream_id_);
                client->stat_->AppendRecvDataSize(static_cast<std::int64_t>(message->ByteSizeLong()));
                if (client->raw_msg_cbk_) client->raw_msg_cbk_(std::move(message));
            });
            adapter->SetMediaCallbacks(
                [owner, generation](EncodedVideoDelivery delivery) {
                    const auto client = owner.lock();
                    if (!client || !client->IsCurrentManagedMediaConnection(generation) || !client->params_.enable_video_ || !delivery.message)
                        return;
                    client->stat_->AppendRecvDataSize(static_cast<std::int64_t>(delivery.message->ByteSizeLong()));
                    if (client->raw_msg_cbk_) client->raw_msg_cbk_(delivery.message);
                    if (client->video_frame_cbk_) client->video_frame_cbk_(std::move(delivery));
                },
                [owner, generation](std::shared_ptr<Message> message) {
                    const auto client = owner.lock();
                    if (!client || !client->IsCurrentManagedMediaConnection(generation) || !client->params_.enable_audio_) return;
                    client->stat_->AppendRecvDataSize(static_cast<std::int64_t>(message->ByteSizeLong()));
                    if (client->audio_frame_cbk_) client->audio_frame_cbk_(std::move(message));
                });
        }
        {
            std::lock_guard lock(client->media_connection_mutex_);
            if (client->exited_) return;
            client->media_conn_ = adapter;
        }
        client->StartManagedConnection(adapter, generation);
    });
    {
        std::lock_guard lock(media_connection_mutex_);
        if (exited_) return;
        iroh_dialer_ = dialer;
    }
    dialer->Start();
}

void NetClient::StopIrohConnection() {
    std::shared_ptr<IrohDialer> dialer{};
    {
        std::lock_guard lock(media_connection_mutex_);
        dialer = std::move(iroh_dialer_);
    }
    if (dialer) dialer->Stop();
}
}  // namespace px
