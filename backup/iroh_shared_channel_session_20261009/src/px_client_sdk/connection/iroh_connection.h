#pragma once

#include "connection.h"
#include "px_transport/session_open.h"

namespace px {
class Thread;

// Adopts an admitted business connection. Endpoint dialing and Console authorization belong to the session workflow.
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
    bool IsAlive() override;
    void SetDatagramCallback(std::function<void(transport::Bytes)> callback);
    [[nodiscard]] bool SendDatagram(std::span<const std::uint8_t> payload);

private:
    struct ChannelWorkers final {
        std::shared_ptr<transport::Channel> channel{};
        std::shared_ptr<Thread> sender{};
        std::shared_ptr<Thread> receiver{};
        std::atomic_size_t queued_bytes{};
    };
    static void ReceiveMessages(std::weak_ptr<IrohConnection> owner, std::shared_ptr<transport::Channel> channel);
    static void ReceiveDatagrams(std::weak_ptr<IrohConnection> owner, std::shared_ptr<transport::Connection> connection);
    void ReportClosed();

    std::shared_ptr<transport::Connection> transport_connection_{};
    std::map<transport::ChannelKind, std::shared_ptr<ChannelWorkers>> channels_{};
    std::mutex operation_mutex_{};
    std::shared_ptr<Thread> datagram_worker_{};
    std::shared_ptr<Thread> dispatch_worker_{};
    std::atomic_size_t queued_receive_bytes_{};
    std::atomic_bool stopped_{true};
    bool started_once_{};
    std::atomic_bool closed_reported_{false};
    std::function<void(transport::Bytes)> datagram_callback_{};
};
}  // namespace px
