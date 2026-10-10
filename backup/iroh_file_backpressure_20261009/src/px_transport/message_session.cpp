#include "message_session.h"

#include "px_common/data.h"
#include "px_common/thread.h"
#include "px_message.pb.h"

namespace px::transport {
constexpr std::size_t kMaximumQueuedBytes{4 * 1024 * 1024};

ChannelKind MessageChannel(const std::shared_ptr<Data>& payload) {
    Message envelope{};
    if (!envelope.ParseFromArray(payload->Bytes().data(), static_cast<int>(payload->Size()))) return ChannelKind::kControl;
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
            return ChannelKind::kInput;
        case kClipboardInfo:
        case kClipboardInfoResp:
        case kClipboardReqAtBegin:
        case kClipboardReqBuffer:
        case kClipboardReqAtEnd:
        case kClipboardRespBuffer:
            return ChannelKind::kClipboard;
        case kFileAction:
        case kFileResponse:
            return ChannelKind::kFile;
        case kRdpStream:
            return ChannelKind::kRdp;
        default:
            return ChannelKind::kControl;
    }
}

namespace {
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

MessageSession::MessageSession(std::shared_ptr<Connection> connection, SessionChannels channels, MessageSessionCallbacks callbacks)
    : connection_(std::move(connection)), callbacks_(std::move(callbacks)) {
    for (auto& [kind, channel] : channels) {
        const auto workers = std::make_shared<ChannelWorkers>();
        workers->channel = std::move(channel);
        channels_.emplace(kind, workers);
    }
}

MessageSession::~MessageSession() { Stop(); }

bool MessageSession::Start() {
    {
        std::lock_guard lock(operation_mutex_);
        if (!connection_ || !channels_.contains(ChannelKind::kControl) || started_once_) return false;
        for (const auto& [kind, workers] : channels_) {
            if (!workers->channel || workers->channel->Kind() != kind) return false;
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
        if (callbacks_.datagram) {
            datagram_worker_ =
                Thread::MakeOnceTask([weak_owner, connection = connection_] { ReceiveDatagrams(weak_owner, connection); }, "iroh-datagram-receive");
        }
    }
    return !stopped_;
}

void MessageSession::Stop() {
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
    if (connection_) connection_->Close();
    for (const auto& worker : workers) {
        if (worker) worker->Exit();
    }
}

void MessageSession::Send(std::shared_ptr<Data> payload, std::function<void(bool)> completion) {
    const auto payload_size = payload ? payload->Size() : 0;
    if (stopped_ || payload_size == 0 || payload_size > Channel::kMaximumMessage) {
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
    ++queued_messages_;
    const auto weak_owner = weak_from_this();
    const auto pending =
        std::make_shared<PendingIrohWrite>([weak_owner, channel_workers, payload_size, completion = std::move(completion)](bool delivered) {
            channel_workers->queued_bytes.fetch_sub(payload_size);
            if (const auto owner = weak_owner.lock()) {
                --owner->queued_messages_;
                if (owner->callbacks_.writable) owner->callbacks_.writable();
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

bool MessageSession::IsAlive() const { return !stopped_ && !closed_reported_; }

bool MessageSession::SendDatagram(std::span<const std::uint8_t> payload) {
    return !stopped_ && connection_ && connection_->SendDatagram(payload).has_value();
}

void MessageSession::ReportClosed() {
    if (!stopped_ && !closed_reported_.exchange(true)) {
        Stop();
        if (callbacks_.closed) callbacks_.closed();
    }
}

void MessageSession::ReceiveMessages(std::weak_ptr<MessageSession> owner, std::shared_ptr<Channel> channel) {
    for (;;) {
        auto received = channel->Receive(1000);
        const auto connection = owner.lock();
        if (!connection || connection->stopped_) return;
        if (!received) {
            if (received.error() == Error::kTimeout) continue;
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
        dispatcher->Post([owner, pending, kind = channel->Kind(), payload = std::move(*received)] {
            const auto adapter = owner.lock();
            if (!adapter || adapter->stopped_) return;
            const auto message = Data::From(std::string_view{reinterpret_cast<const char*>(payload.data()), payload.size()});
            if (MessageChannel(message) != kind) {
                adapter->ReportClosed();
                return;
            }
            if (adapter->callbacks_.message) adapter->callbacks_.message(kind, message);
        });
    }
}

void MessageSession::ReceiveDatagrams(std::weak_ptr<MessageSession> owner, std::shared_ptr<Connection> connection) {
    for (;;) {
        auto received = connection->ReceiveDatagram(1000);
        const auto adapter = owner.lock();
        if (!adapter || adapter->stopped_) return;
        if (!received) {
            if (received.error() == Error::kTimeout) continue;
            adapter->ReportClosed();
            return;
        }
        if (adapter->callbacks_.datagram) adapter->callbacks_.datagram(std::move(*received));
    }
}
}  // namespace px::transport
