#include "iroh_server.h"

#include <algorithm>
#include <future>

#include "px_common/async_delay.h"
#include "px_common/log.h"
#include "px_common/thread.h"

namespace px {
namespace {
constexpr std::size_t kMaximumPendingPeers{16};
constexpr std::size_t kMaximumActivePeers{128};

PxAwaitable<void> CompleteAdmission(std::weak_ptr<WsTransport> services, std::shared_ptr<WebSocketFrontendLeaseRenewalCoordinator> leases,
                                    std::shared_ptr<transport::Connection> connection, transport::FrontendParameters parameters, bool rdp,
                                    RenderEventCallback events, std::shared_ptr<std::promise<PxResult<std::shared_ptr<IrohFrontend>>>> completion) {
    try {
        completion->set_value(co_await IrohFrontend::AdmitAsync(services, leases, connection, std::move(parameters), rdp, std::move(events)));
    } catch (const std::exception&) {
        completion->set_value(PxResult<std::shared_ptr<IrohFrontend>>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "iroh_frontend_admit", "Admission stopped")));
    }
}

void RejectFrontend(const std::shared_ptr<transport::Connection>& connection, const std::shared_ptr<transport::Channel>& control,
                    const std::string& code) {
    const auto response = transport::EncodeSessionOpenReply({.accepted = false, .code = code});
    if (response && control->Send(*response, 1000)) {
        // Let the caller consume the rejection before QUIC CLOSE discards outstanding writes.
        static_cast<void>(control->Finish(1000));
        static_cast<void>(control->Receive(1000));
    }
    connection->Close();
}
}  // namespace

IrohServer::IrohServer(std::weak_ptr<WsTransport> services, std::shared_ptr<PxAsyncRuntime> runtime, bool rdp, AcceptedCallback accepted,
                       RenderEventCallback events)
    : services_(std::move(services)), scope_(PxAsyncScope::Create(runtime)), rdp_(rdp), accepted_(std::move(accepted)), events_(std::move(events)) {
    leases_ = std::make_shared<WebSocketFrontendLeaseRenewalCoordinator>(services_, scope_);
}

IrohServer::~IrohServer() { Stop(); }

bool IrohServer::Start(const std::string& endpoint_configuration) {
    std::lock_guard lock(mutex_);
    if (started_once_ || !accepted_ || !events_ || services_.expired()) return false;
    started_once_ = true;
    endpoint_ = transport::Endpoint::Bind(endpoint_configuration, 10000);
    if (!endpoint_) return false;
    stopped_ = false;
    const auto owner = weak_from_this();
    if (!scope_->Spawn("iroh-connection-lifecycle", [owner] { return MonitorConnections(owner); })) {
        endpoint_->Close();
        stopped_ = true;
        return false;
    }
    accept_worker_ = Thread::MakeOnceTask([owner = weak_from_this(), endpoint = endpoint_] { AcceptConnections(owner, endpoint); }, "iroh-accept");
    return true;
}

void IrohServer::Stop() {
    std::shared_ptr<Thread> accept_worker{};
    std::vector<std::shared_ptr<PendingPeer>> pending{};
    std::vector<std::shared_ptr<IrohFrontend>> active{};
    {
        std::lock_guard lock(mutex_);
        if (stopped_.exchange(true)) return;
        if (endpoint_) endpoint_->Close();
        accept_worker = std::move(accept_worker_);
        pending.swap(pending_);
        active.swap(active_);
    }
    for (const auto& peer : pending) peer->connection->Close();
    for (const auto& frontend : active) frontend->Close(ResourceChannelCloseOutcome::kUserStopped);
    if (accept_worker) accept_worker->Exit();
    for (const auto& peer : pending) {
        if (peer->worker) peer->worker->Exit();
    }
    scope_->BeginStop();
}

std::expected<std::string, transport::Error> IrohServer::Address() const {
    std::lock_guard lock(mutex_);
    if (stopped_ || !endpoint_) return std::unexpected(transport::Error::kClosed);
    return endpoint_->Address();
}

bool IrohServer::UpdateRelays(const std::string& relays_json) {
    std::lock_guard lock(mutex_);
    return !stopped_ && endpoint_ && endpoint_->UpdateRelays(relays_json).has_value();
}

void IrohServer::PruneConnections() {
    std::vector<std::shared_ptr<PendingPeer>> completed{};
    std::vector<std::shared_ptr<IrohFrontend>> disconnected{};
    {
        std::lock_guard lock(mutex_);
        for (auto pending = pending_.begin(); pending != pending_.end();) {
            if ((*pending)->done) {
                completed.push_back(std::move(*pending));
                pending = pending_.erase(pending);
            } else {
                ++pending;
            }
        }
        for (auto frontend = active_.begin(); frontend != active_.end();) {
            if ((*frontend)->IsClosed()) {
                disconnected.push_back(std::move(*frontend));
                frontend = active_.erase(frontend);
            } else {
                ++frontend;
            }
        }
    }
    for (const auto& peer : completed) {
        if (peer->worker) peer->worker->Exit();
    }
    for (const auto& frontend : disconnected) frontend->Close();
}

void IrohServer::AcceptConnections(std::weak_ptr<IrohServer> owner, std::shared_ptr<transport::Endpoint> endpoint) {
    for (;;) {
        // Accept includes the QUIC handshake; do not use the lifecycle polling interval as its deadline.
        const auto connection = endpoint->Accept(10000);
        const auto server = owner.lock();
        if (!server || server->stopped_) {
            if (connection) connection->Close();
            return;
        }
        if (!connection) continue;
        std::lock_guard lock(server->mutex_);
        if (server->stopped_ || server->pending_.size() >= kMaximumPendingPeers || server->active_.size() >= kMaximumActivePeers) {
            connection->Close();
            continue;
        }
        const auto peer = std::make_shared<PendingPeer>();
        peer->connection = connection;
        peer->worker = Thread::MakeOnceTask(
            [owner, peer] {
                try {
                    OpenPeer(owner, peer);
                } catch (const std::exception&) {
                    peer->connection->Close();
                    LOGW("event=session.admit component=net_iroh outcome=failed code=TRANSPORT_INTERNAL_ERROR");
                }
                peer->done = true;
            },
            "iroh-frontend-open");
        server->pending_.push_back(peer);
    }
}

PxAwaitable<void> IrohServer::MonitorConnections(std::weak_ptr<IrohServer> owner) {
    for (;;) {
        const auto waited = co_await WaitForAsyncDelay(std::chrono::milliseconds(250), "iroh_connection_lifecycle");
        const auto server = owner.lock();
        if (!waited || !server || server->stopped_) co_return;
        server->PruneConnections();
    }
}

void IrohServer::OpenPeer(std::weak_ptr<IrohServer> owner, std::shared_ptr<PendingPeer> peer) {
    const auto control = transport::Channel::Accept(peer->connection, 5000);
    if (!control || (*control)->Kind() != transport::ChannelKind::kControl) {
        peer->connection->Close();
        return;
    }
    auto request = (*control)->Receive(5000);
    if (!request) {
        peer->connection->Close();
        return;
    }
    auto parameters = transport::DecodeSessionOpen(*request);
    std::fill(request->begin(), request->end(), 0);
    if (!parameters) {
        RejectFrontend(peer->connection, *control, "SESSION_REQUEST_INVALID");
        return;
    }
    const auto admission_server = owner.lock();
    if (!admission_server || admission_server->stopped_) {
        peer->connection->Close();
        return;
    }
    std::unique_lock rdp_admission(admission_server->rdp_admission_mutex_, std::defer_lock);
    if (admission_server->rdp_) {
        rdp_admission.lock();
        const auto existing = admission_server->rdp_frontend_.lock();
        if (existing && !existing->IsClosed()) {
            RejectFrontend(peer->connection, *control, "SESSION_OCCUPIED");
            return;
        }
    }
    using AdmissionResult = PxResult<std::shared_ptr<IrohFrontend>>;
    const auto completion = std::make_shared<std::promise<AdmissionResult>>();
    auto admitted = completion->get_future();
    {
        const auto server = owner.lock();
        if (!server || server->stopped_) {
            peer->connection->Close();
            return;
        }
        const bool spawned = server->scope_->Spawn(
            "iroh-frontend-admission", [services = server->services_, leases = server->leases_, connection = peer->connection,
                                        parameters = std::move(*parameters), rdp = server->rdp_, events = server->events_, completion]() mutable {
                return CompleteAdmission(services, leases, connection, std::move(parameters), rdp, std::move(events), completion);
            });
        if (!spawned) {
            RejectFrontend(peer->connection, *control, "TRANSPORT_UNAVAILABLE");
            return;
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (admitted.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        if (peer->connection->IsClosed() || std::chrono::steady_clock::now() >= deadline) {
            peer->connection->Close();
            return;
        }
    }
    auto result = admitted.get();
    if (!result.HasValue()) {
        LOGW("event=session.admit component=net_iroh outcome=rejected code={}", result.Error().StableCode());
        RejectFrontend(peer->connection, *control, result.Error().StableCode());
        return;
    }
    const auto frontend = result.TakeValue();
    const auto server = owner.lock();
    if (!server || server->stopped_ || frontend->IsClosed()) return;
    if (frontend->IsRdp()) {
        bool occupied{};
        {
            std::lock_guard lock(server->mutex_);
            const auto existing = server->rdp_frontend_.lock();
            occupied = existing && !existing->IsClosed();
            if (!occupied) server->rdp_frontend_ = frontend;
        }
        if (occupied) {
            RejectFrontend(peer->connection, *control, "SESSION_OCCUPIED");
            return;
        }
    }
    const auto channel_kinds = frontend->Channels();
    const auto reply = transport::EncodeSessionOpenReply(
        {.accepted = true, .code = "SESSION_ACCEPTED", .stream_id = frontend->StreamId(), .channels = channel_kinds});
    if (!reply || !(*control)->Send(*reply, 5000)) return;
    transport::SessionChannels channels{{transport::ChannelKind::kControl, *control}};
    while (channels.size() < channel_kinds.size()) {
        const auto channel = transport::Channel::Accept(peer->connection, 5000);
        if (!channel || std::ranges::find(channel_kinds, (*channel)->Kind()) == channel_kinds.end() || channels.contains((*channel)->Kind())) return;
        channels.emplace((*channel)->Kind(), *channel);
    }
    // The router owns its queues before connected/configuration events can target this session.
    if (!server->accepted_({frontend, peer->connection, *control, std::move(channels)}) || !frontend->Activate()) {
        frontend->Close();
        return;
    }
    {
        std::lock_guard lock(server->mutex_);
        if (server->stopped_) {
            frontend->Close();
            return;
        }
        server->active_.push_back(frontend);
    }
    LOGI("event=session.admit component=net_iroh outcome=connected");
}
}  // namespace px
