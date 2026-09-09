#include "network/ws/ws_stream_router.h"
#include "px_rdp/rdp_stream_packet.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace px {
namespace {
using namespace std::chrono_literals;

struct RouteProbe final {
    std::atomic_int protocol_closed{0};
    std::atomic_int released{0};
    std::atomic_bool opened{false};
};

TEST(RdpRouteClose, ProtocolCloseReleasesSeatBeforeWebSocketDisconnect) {
    for (int cycle{0}; cycle < 4; ++cycle) {
        const auto probe = std::make_shared<RouteProbe>();
        const auto proxy = std::make_shared<asio2::tcp_server>();
        const auto server = std::make_shared<asio2::http_server>();
        const auto client = std::make_shared<asio2::ws_client>();
        const auto data = std::make_shared<WsData>();
        const auto router = WsStreamRouter::Make(data, false, "test-visitor", "test-stream");
        ASSERT_TRUE(proxy->start("127.0.0.1", 0));
        const auto port = proxy->get_listen_port();
        const auto weak_router = std::weak_ptr<WsStreamRouter>{router};
        server->support_websocket(true);
        server->bind("/media", websocket::listener<asio2::http_session>{}
                                   .on("open",
                                       [weak_router, probe, port](std::shared_ptr<asio2::http_session>& session) {
                                           const auto route = weak_router.lock();
                                           if (!route) {
                                               return;
                                           }
                                           route->OnOpen(session);
                                           EXPECT_TRUE(route->StartRdp(
                                               session->io().context().get_executor(), port, [probe] { ++probe->released; },
                                               [weak_router, probe, weak_session = std::weak_ptr<asio2::http_session>{session}] {
                                                   if (const auto active = weak_router.lock()) {
                                                       auto peer = weak_session.lock();
                                                       if (peer) {
                                                           // This callback is the application close boundary,
                                                           // deliberately independent of WS close/disconnect events.
                                                           active->OnClose(peer);
                                                           ++probe->protocol_closed;
                                                       }
                                                   }
                                               }));
                                       })
                                   .on("message", [weak_router](std::shared_ptr<asio2::http_session>& session, std::string_view wire) {
                                       if (const auto route = weak_router.lock()) {
                                           route->OnMessage(session, 1, wire);
                                       }
                                   }));
        ASSERT_TRUE(server->start("127.0.0.1", 0));
        client->bind_recv([weak_client = std::weak_ptr<asio2::ws_client>{client}, probe](std::string_view wire) {
            if (const auto binding = rdp::DecodeOpen(wire); binding && !probe->opened.exchange(true)) {
                if (const auto peer = weak_client.lock()) {
                    peer->ws_stream().binary(true);
                    peer->async_send(rdp::EncodeClose(*binding)->AsString());
                }
            }
        });
        ASSERT_TRUE(client->start("127.0.0.1", server->get_listen_port(), "/media"));
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (probe->protocol_closed.load() == 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }
        EXPECT_TRUE(probe->opened.load());
        EXPECT_EQ(probe->protocol_closed.load(), 1);
        EXPECT_EQ(probe->released.load(), 1);
        client->stop();
        server->stop();
        proxy->stop();
        EXPECT_EQ(probe->released.load(), 1);
    }
}
} // namespace
} // namespace px
