#pragma once

#include <atomic>
#include <functional>
#include <mutex>

#include "px_common/file_transfer_send_result.h"
#include "session_open.h"

namespace px {
class Data;
class Thread;
}  // namespace px

namespace px::transport {

// Business message classification shared by both ends; channel identity is also checked on receipt.
[[nodiscard]] ChannelKind MessageChannel(const std::shared_ptr<Data>& payload);

struct MessageSessionCallbacks final {
    std::function<void(ChannelKind, std::shared_ptr<Data>)> message{};
    std::function<void(Bytes)> datagram{};
    std::function<void()> closed{};
    std::function<void()> writable{};
};

// One bounded send queue per reliable stream; message callbacks run on a serial dispatcher.
// Shutdown may be called from any callback. No callback owns the enclosing Client/Render session.
class MessageSession final : public std::enable_shared_from_this<MessageSession> {
public:
    MessageSession(std::shared_ptr<Connection> connection, SessionChannels channels, MessageSessionCallbacks callbacks);
    ~MessageSession();
    [[nodiscard]] bool Start();
    void Stop();
    void Send(std::shared_ptr<Data> payload, std::function<void(bool)> completion = {});
    [[nodiscard]] FileTransferSendResult SendFile(std::shared_ptr<Data> payload, std::function<void(bool)> completion = {});
    [[nodiscard]] bool IsAlive() const;
    [[nodiscard]] bool SendDatagram(std::span<const std::uint8_t> payload);
    [[nodiscard]] std::size_t QueuedMessages() const { return queued_messages_; }

private:
    struct ChannelWorkers final {
        std::shared_ptr<Channel> channel{};
        std::shared_ptr<Thread> sender{};
        std::shared_ptr<Thread> receiver{};
        std::atomic_size_t queued_bytes{};
        std::shared_ptr<FileTransferWritableSignal> writable{};
    };
    static void ReceiveMessages(std::weak_ptr<MessageSession> owner, std::shared_ptr<Channel> channel);
    static void ReceiveDatagrams(std::weak_ptr<MessageSession> owner, std::shared_ptr<Connection> connection);
    enum class SendStatus { kAccepted, kBusy, kClosed, kInvalid };
    [[nodiscard]] SendStatus Enqueue(std::shared_ptr<Data> payload, std::function<void(bool)> completion);
    [[nodiscard]] std::shared_ptr<FileTransferWritableSignal> FileWritableSignal();
    void NotifyWritable(const std::shared_ptr<ChannelWorkers>& channel);
    void ReportClosed();
    std::shared_ptr<Connection> connection_{};
    std::map<ChannelKind, std::shared_ptr<ChannelWorkers>> channels_{};
    MessageSessionCallbacks callbacks_{};
    std::mutex operation_mutex_{};
    std::shared_ptr<Thread> datagram_worker_{};
    std::shared_ptr<Thread> dispatch_worker_{};
    std::atomic_size_t queued_receive_bytes_{};
    std::atomic_size_t queued_messages_{};
    std::atomic_bool stopped_{true};
    bool started_once_{};
    std::atomic_bool closed_reported_{};
};
}  // namespace px::transport
