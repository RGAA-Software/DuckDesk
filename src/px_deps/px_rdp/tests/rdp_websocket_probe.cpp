// Opt-in, time-bounded LAN integration probe, never linked into product runtimes.
// server: WS /stream :13390 -> local proxy :13389
// client: protected-by-test-scope loopback :13391 -> WS /stream :13390
// The deployment harness must restrict the server firewall to the test console.
#include <asio2/http/http_server.hpp>
#include <asio2/websocket/ws_client.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "px_rdp/rdp_tcp_bridge.h"

namespace {

using namespace px::rdp;
using namespace std::chrono_literals;

std::string Environment(const std::string& name, const std::string& fallback = {}) {
    // Transient C runtime boundary; no environment pointer is stored or captured.
    return std::getenv(name.c_str()) ? std::string(std::getenv(name.c_str())) : fallback;
}

struct ProbeState final {
    std::mutex mutex{};
    std::shared_ptr<RdpTcpBridge> bridge{};
    std::weak_ptr<asio2::http_session> accepted_session{};
    std::shared_ptr<px::PxAsyncRuntime> runtime{};
    StreamBinding binding{};
    std::atomic_bool active{false};
    std::atomic_bool failed{false};
    std::atomic_uint64_t sent_bytes{0};
    std::atomic_uint64_t received_bytes{0};

    std::shared_ptr<RdpTcpBridge> Snapshot() {
        std::lock_guard lock(mutex);
        return bridge;
    }

    void SetBridge(std::shared_ptr<RdpTcpBridge> value) {
        std::lock_guard lock(mutex);
        bridge = std::move(value);
    }

    bool IsAcceptedSession(const std::shared_ptr<asio2::http_session>& session) {
        std::lock_guard lock(mutex);
        return accepted_session.lock() == session;
    }

    void SetAcceptedSession(const std::shared_ptr<asio2::http_session>& session) {
        std::lock_guard lock(mutex);
        accepted_session = session;
    }

    void Receive(const std::string_view data) {
        received_bytes.fetch_add(data.size());
        if (const auto current = Snapshot()) {
            static_cast<void>(current->Receive(px::Data::From(data)));
        }
    }
};

RdpTcpBridge::Closed ClosedCallback(const std::shared_ptr<ProbeState>& state) {
    return [weak = std::weak_ptr<ProbeState>(state)](const BridgeCloseReason reason) {
        if (const auto current = weak.lock()) {
            std::cout << "RDP stream closed reason=" << static_cast<int>(reason) << std::endl;
            if (reason != BridgeCloseReason::kStopped && reason != BridgeCloseReason::kPeerClosed && reason != BridgeCloseReason::kTcpClosed) {
                current->failed.store(true);
            }
        }
    };
}

template <typename Socket> RdpTcpBridge::Send Sender(const std::shared_ptr<ProbeState>& state, const std::shared_ptr<Socket>& socket) {
    return [weak_state = std::weak_ptr<ProbeState>(state), weak_socket = std::weak_ptr<Socket>(socket)](std::shared_ptr<px::Data> wire,
                                                                                                        RdpTcpBridge::SendCompletion complete) {
        const auto transport = weak_socket.lock();
        if (!transport || !transport->is_started()) {
            complete(false);
            return;
        }
        transport->post([weak_state, weak_socket, wire = std::move(wire), complete = std::move(complete)] {
            const auto current = weak_socket.lock();
            if (!current || !current->is_started()) {
                complete(false);
                return;
            }
            current->ws_stream().binary(true);
            current->async_send(wire->Bytes().data(), wire->Size(), [weak_state, wire, complete](const std::size_t written) {
                if (const auto owner = weak_state.lock()) {
                    owner->sent_bytes.fetch_add(written);
                }
                complete(written == wire->Size() && !asio2::get_last_error());
            });
        });
    };
}

std::shared_ptr<asio2::http_server> StartServer(const std::shared_ptr<ProbeState>& state, const std::string& host) {
    const auto server = std::make_shared<asio2::http_server>();
    const auto weak = std::weak_ptr<ProbeState>(state);
    server->support_websocket(true);
    server->bind("/stream", websocket::listener<asio2::http_session>{}
                                .on("open",
                                    [weak](std::shared_ptr<asio2::http_session>& session) {
                                        const auto current = weak.lock();
                                        if (!current || current->active.exchange(true)) {
                                            session->stop();
                                            return;
                                        }
                                        current->SetAcceptedSession(session);
                                        const auto bridge = RdpTcpBridge::Create(current->runtime->Executor(px::PxAsyncLane::kWorker),
                                                                                 current->binding, Sender(current, session), ClosedCallback(current));
                                        current->SetBridge(bridge);
                                        bridge->ConnectLoopback(13389);
                                    })
                                .on("message",
                                    [weak](std::shared_ptr<asio2::http_session>& session, const std::string_view data) {
                                        if (const auto current = weak.lock(); current && current->IsAcceptedSession(session)) {
                                            current->Receive(data);
                                        }
                                    })
                                .on("close", [weak](std::shared_ptr<asio2::http_session>& session) {
                                    if (const auto current = weak.lock(); current && current->IsAcceptedSession(session)) {
                                        if (const auto bridge = current->Snapshot()) {
                                            bridge->Stop();
                                        }
                                        // One WS binding per process run: restart with a new nonce for reconnect tests.
                                    }
                                }));
    if (!server->start(host, 13390)) {
        return {};
    }
    return server;
}

struct ClientResources final {
    std::shared_ptr<asio2::ws_client> websocket{};
    std::shared_ptr<asio::ip::tcp::acceptor> acceptor{};
};

ClientResources StartClient(const std::shared_ptr<ProbeState>& state, const std::string& host) {
    const auto client = std::make_shared<asio2::ws_client>();
    client->set_auto_reconnect(false);
    client->set_connect_timeout(3s);
    const auto weak = std::weak_ptr<ProbeState>(state);
    client->bind_recv([weak](const std::string_view data) {
        if (const auto current = weak.lock()) {
            current->Receive(data);
        }
    });
    client->bind_disconnect([weak] {
        if (const auto current = weak.lock()) {
            if (const auto bridge = current->Snapshot()) {
                bridge->Stop();
            }
        }
    });
    if (!client->start(host, 13390, "/stream")) {
        return {};
    }
    const auto executor = state->runtime->Executor(px::PxAsyncLane::kWorker);
    const auto acceptor = std::make_shared<asio::ip::tcp::acceptor>(executor, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 13391));
    const auto socket = std::make_shared<asio::ip::tcp::socket>(executor);
    const auto weak_client = std::weak_ptr<asio2::ws_client>(client);
    acceptor->async_accept(*socket, [weak, weak_client, socket](const asio::error_code& error) {
        const auto current = weak.lock();
        const auto transport = weak_client.lock();
        if (!current || !transport || error) {
            return;
        }
        const auto bridge = RdpTcpBridge::Create(current->runtime->Executor(px::PxAsyncLane::kWorker), current->binding, Sender(current, transport),
                                                 ClosedCallback(current));
        current->SetBridge(bridge);
        bridge->Attach(socket);
    });
    return {.websocket = client, .acceptor = acceptor};
}

} // namespace

int main() {
    const auto role = Environment("RDP_PROBE_ROLE");
    const auto host = Environment("RDP_PROBE_HOST", "127.0.0.1");
    const auto state = std::make_shared<ProbeState>();
    state->binding = {.connection_id = Environment("RDP_PROBE_BINDING"), .generation = 1};
    if ((role != "server" && role != "client") || (host != "127.0.0.1" && host != "10.0.0.90") || !state->binding.IsValid() ||
        state->binding.connection_id.size() < 16) {
        std::cerr << "Set RDP_PROBE_ROLE=server/client, RDP_PROBE_HOST, and a shared random RDP_PROBE_BINDING (16..128 bytes)." << std::endl;
        return 2;
    }
    state->runtime = px::PxAsyncRuntime::Create();
    if (!state->runtime->Start()) {
        return 3;
    }
    auto server = std::shared_ptr<asio2::http_server>{};
    auto client = ClientResources{};
    try {
        if (role == "server") {
            server = StartServer(state, host);
        } else {
            client = StartClient(state, host);
        }
        if (!server && !client.websocket) {
            state->failed.store(true);
        } else {
            std::cout << "ready role=" << role << " lifetime_seconds=180" << std::endl;
            const auto deadline = std::chrono::steady_clock::now() + 180s;
            while (!state->failed.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(50ms);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "probe setup failed: " << error.what() << std::endl;
        state->failed.store(true);
    }
    if (const auto bridge = state->Snapshot()) {
        bridge->Stop();
    }
    if (client.acceptor) {
        asio::error_code error{};
        client.acceptor->cancel(error);
        client.acceptor->close(error);
    }
    if (client.websocket) {
        client.websocket->stop();
    }
    if (server) {
        server->stop();
    }
    state->runtime->RequestStop();
    state->runtime->Join();
    state->SetBridge({});
    std::cout << "done sent_bytes=" << state->sent_bytes.load() << " received_bytes=" << state->received_bytes.load() << std::endl;
    return state->failed.load() ? 1 : 0;
}
