#include "iroh_connection.h"

#include "px_common/data.h"
#include "px_common/thread.h"
#include "px_message.pb.h"

namespace px {
namespace {
constexpr std::size_t kMaximumQueuedBytes{4 * 1024 * 1024};

transport::ChannelKind MessageChannel(const std::shared_ptr<Data>& payload) {
    Message envelope{};
    if (!envelope.ParseFromArray(payload->Bytes().data(), static_cast<int>(payload->Size()))) return transport::ChannelKind::kControl;
    switch (envelope.type()) {
        case kMouseEvent:
        case kKeyEvent:
        case kGamepadState:
        case kGamepadRumble:
        case kFocusOutEvent:
        case kTextInput:
        case kApplicationTextCapabilities:
        case kApplicationTextState:
        case kApplicationTextSubmit:
        case kApplicationTextResult:
        case kApplicationTextBarrier:
        case kApplicationTextBarrierResult:
            return transport::ChannelKind::kInput;
        case kClipboardInfo:
        case kClipboardInfoResp:
        case kClipboardReqAtBegin:
        case kClipboardReqBuffer:
        case kClipboardReqAtEnd:
        case kClipboardRespBuffer:
            return transport::ChannelKind::kClipboard;
        case kFileAction:
        case kFileResponse:
            return transport::ChannelKind::kFile;
        case kRdpStream:
            return transport::ChannelKind::kRdp;
        default:
            return transport::ChannelKind::kControl;
    }
}

class PendingIrohWrite final {
public:
    explicit PendingIrohWrite(std::function<void(bool)> completion) : completion_(std::move(completion)) {}
    ~PendingIrohWrite() { Complete(false); }
    void Complete(bool delivered) {
        if (const auto completion = std::exchange(completion_, {})) completion(delivered);
    }

private:
    std::function<void(bool)> completion_{};
};
}  // namespace

IrohConnection::IrohConnection(const std::shared_ptr<MessageNotifier>& notifier, std::shared_ptr<transport::Connection> connection,
                               std::shared_ptr<transport::Channel> channel)
    : IrohConnection(notifier, std::move(connection), transport::SessionChannels{{transport::ChannelKind::kControl, std::move(channel)}}) {}

IrohConnection::IrohConnection(const std::shared_ptr<MessageNotifier>& notifier, std::shared_ptr<transport::Connection> connection,
                               transport::SessionChannels channels)
    : Connection(notifier), transport_connection_(std::move(connection)) {
    for (auto& [kind, channel] : channels) {
        const auto workers = std::make_shared<ChannelWorkers>();
        workers->channel = std::move(channel);
        channels_.emplace(kind, workers);
    }
}

IrohConnection::~IrohConnection() { Stop(); }

void IrohConnection::Start() {
    {
        std::lock_guard lock(operation_mutex_);
        if (!transport_connection_ || !channels_.contains(transport::ChannelKind::kControl) || started_once_) return;
        for (const auto& [kind, workers] : channels_) {
            if (!workers->channel || workers->channel->Kind() != kind) return;
        }
        started_once_ = true;
        stopped_ = false;
        closed_reported_ = false;
        const auto weak_owner = weak_from_this();
        dispatch_worker_ = Thread::Make("iroh-message-dispatch", -1);
        dispatch_worker_->Poll();
        for (const auto& [kind, workers] : channels_) {
            const auto label = std::to_string(static_cast<int>(kind));
            workers->sender = Thread::Make("iroh-send-" + label, -1);
            workers->sender->Poll();
            workers->receiver =
                Thread::MakeOnceTask([weak_owner, channel = workers->channel] { ReceiveMessages(weak_owner, channel); }, "iroh-receive-" + label);
        }
        if (datagram_callback_) {
            datagram_worker_ = Thread::MakeOnceTask([weak_owner, connection = transport_connection_] { ReceiveDatagrams(weak_owner, connection); },
                                                    "iroh-datagram-receive");
        }
    }
    if (!stopped_ && conn_cbk_) conn_cbk_();
}

void IrohConnection::Stop() {
    std::vector<std::shared_ptr<Thread>> workers{};
    {
        std::lock_guard lock(operation_mutex_);
        if (stopped_.exchange(true)) return;
        for (const auto& [kind, channel_workers] : channels_) {
            workers.push_back(std::move(channel_workers->sender));
            workers.push_back(std::move(channel_workers->receiver));
        }
        workers.push_back(std::move(datagram_worker_));
        workers.push_back(std::move(dispatch_worker_));
    }
    if (transport_connection_) transport_connection_->Close();
    NotifyFileTransferClosed();
    for (const auto& worker : workers) {
        if (worker) worker->Exit();
    }
}

void IrohConnection::PostBinaryMessage(std::shared_ptr<Data> payload) { PostReliableBinaryMessage(std::move(payload), {}); }

void IrohConnection::PostReliableBinaryMessage(std::shared_ptr<Data> payload, std::function<void(bool)> completion) {
    const auto payload_size = payload ? payload->Size() : 0;
    if (stopped_ || payload_size == 0 || payload_size > transport::Channel::kMaximumMessage) {
        if (completion) completion(false);
        return;
    }
    const auto found = channels_.find(MessageChannel(payload));
    if (found == channels_.end()) {
        if (completion) completion(false);
        return;
    }
    const auto channel_workers = found->second;
    if (channel_workers->queued_bytes.fetch_add(payload_size) + payload_size > kMaximumQueuedBytes) {
        channel_workers->queued_bytes.fetch_sub(payload_size);
        if (completion) completion(false);
        return;
    }
    ++queuing_message_count_;
    const auto weak_owner = weak_from_this();
    const auto pending =
        std::make_shared<PendingIrohWrite>([weak_owner, channel_workers, payload_size, completion = std::move(completion)](bool delivered) {
            channel_workers->queued_bytes.fetch_sub(payload_size);
            if (const auto owner = weak_owner.lock()) {
                --owner->queuing_message_count_;
                owner->NotifyFileTransferWritable();
            }
            if (completion) completion(delivered);
        });
    std::shared_ptr<Thread> worker{};
    {
        std::lock_guard lock(operation_mutex_);
        worker = channel_workers->sender;
    }
    if (!worker || stopped_) return;
    worker->Post([weak_owner, pending, channel = channel_workers->channel, payload = std::move(payload)] {
        const auto owner = weak_owner.lock();
        if (!owner || owner->stopped_) return;
        const auto bytes = payload->Bytes();
        const auto sent = channel->Send(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}, 5000);
        pending->Complete(sent.has_value());
        if (!sent && !owner->stopped_) owner->ReportClosed();
    });
}

bool IrohConnection::IsAlive() { return !stopped_ && !closed_reported_; }

void IrohConnection::SetDatagramCallback(std::function<void(transport::Bytes)> callback) {
    std::lock_guard lock(operation_mutex_);
    if (stopped_) datagram_callback_ = std::move(callback);
}

bool IrohConnection::SendDatagram(std::span<const std::uint8_t> payload) {
    return !stopped_ && transport_connection_ && transport_connection_->SendDatagram(payload).has_value();
}

void IrohConnection::ReportClosed() {
    if (!stopped_ && !closed_reported_.exchange(true)) {
        Stop();
        if (dis_conn_cbk_) dis_conn_cbk_();
    }
}

void IrohConnection::ReceiveMessages(std::weak_ptr<IrohConnection> owner, std::shared_ptr<transport::Channel> channel) {
    for (;;) {
        auto received = channel->Receive(1000);
        const auto connection = owner.lock();
        if (!connection || connection->stopped_) return;
        if (!received) {
            if (received.error() == transport::Error::kTimeout) continue;
            connection->ReportClosed();
            return;
        }
        const auto payload_size = received->size();
        if (connection->queued_receive_bytes_.fetch_add(payload_size) + payload_size > kMaximumQueuedBytes) {
            connection->queued_receive_bytes_.fetch_sub(payload_size);
            connection->ReportClosed();
            return;
        }
        const auto pending = std::make_shared<PendingIrohWrite>([owner, payload_size](bool) {
            if (const auto adapter = owner.lock()) adapter->queued_receive_bytes_.fetch_sub(payload_size);
        });
        std::shared_ptr<Thread> dispatcher{};
        {
            std::lock_guard lock(connection->operation_mutex_);
            dispatcher = connection->dispatch_worker_;
        }
        if (!dispatcher) return;
        // Preserve the SDK's serial callback contract while the independent streams receive concurrently.
        dispatcher->Post([owner, pending, payload = std::move(*received)] {
            const auto adapter = owner.lock();
            if (!adapter || adapter->stopped_) return;
            if (adapter->msg_cbk_) {
                adapter->msg_cbk_(Data::From(std::string_view{reinterpret_cast<const char*>(payload.data()), payload.size()}));
            }
        });
    }
}

void IrohConnection::ReceiveDatagrams(std::weak_ptr<IrohConnection> owner, std::shared_ptr<transport::Connection> connection) {
    for (;;) {
        auto received = connection->ReceiveDatagram(1000);
        const auto adapter = owner.lock();
        if (!adapter || adapter->stopped_) return;
        if (!received) {
            if (received.error() == transport::Error::kTimeout) continue;
            adapter->ReportClosed();
            return;
        }
        if (adapter->datagram_callback_) adapter->datagram_callback_(std::move(*received));
    }
}
}  // namespace px
