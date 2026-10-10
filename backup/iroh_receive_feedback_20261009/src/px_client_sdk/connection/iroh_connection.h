#pragma once

#include "connection.h"
#include "encoded_video_delivery.h"
#include "px_transport/media_datagrams.h"
#include "px_transport/message_session.h"

namespace px {
// Adopts an admitted business connection; dialing/authorization belong to the session workflow.
class IrohConnection final : public Connection, public std::enable_shared_from_this<IrohConnection> {
public:
    IrohConnection(const std::shared_ptr<MessageNotifier>& notifier, std::shared_ptr<transport::Connection> connection,
                   std::shared_ptr<transport::Channel> channel);
    IrohConnection(const std::shared_ptr<MessageNotifier>& notifier, std::shared_ptr<transport::Connection> connection,
                   transport::SessionChannels channels);
    ~IrohConnection() override;
    void Start() override;
    void Stop() override;
    void PostBinaryMessage(std::shared_ptr<Data> payload) override;
    void PostReliableBinaryMessage(std::shared_ptr<Data> payload, std::function<void(bool)> completion) override;
    [[nodiscard]] FileTransferSendResult PostFileTransferMessage(std::shared_ptr<Data> payload) override;
    bool IsAlive() override;
    void SetDatagramCallback(std::function<void(transport::Bytes)> callback);
    void SetMediaCallbacks(std::function<void(EncodedVideoDelivery)> video, std::function<void(std::shared_ptr<Message>)> audio);
    void SetVoiceCallback(std::function<void(std::shared_ptr<Message>)> voice);
    [[nodiscard]] bool SendDatagram(std::span<const std::uint8_t> payload);
    [[nodiscard]] bool SendVoice(const Message& message);

private:
    // Only the media receiver worker updates these counters, including recovery callbacks.
    struct ReceiveWindow final {
        std::optional<std::uint64_t> started_us{};
        std::optional<std::uint64_t> previous_frame_us{};
        std::uint64_t frames{};
        std::uint64_t maximum_gap_us{};
        std::uint64_t gaps_over_100ms{};
        std::uint64_t key_frame_requests{};
        std::uint64_t reference_requests{};
    };
    void ObserveVideo(const media::VideoFrame& frame);
    std::map<std::uint8_t, ReceiveWindow> receive_windows_{};
    std::function<void(std::shared_ptr<Message>)> voice_callback_{};
    std::shared_ptr<transport::Connection> connection_{};
    transport::SessionChannels channels_{};
    std::mutex mutex_{};
    std::shared_ptr<transport::MessageSession> session_{};
    std::shared_ptr<transport::MediaDatagramReceiver> media_receiver_{};
    std::function<void(EncodedVideoDelivery)> video_callback_{};
    std::function<void(std::shared_ptr<Message>)> audio_callback_{};
    std::function<void(transport::Bytes)> datagram_callback_{};
    bool started_once_{};
    std::atomic_bool stop_requested_{};
};
}  // namespace px
