#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include <Windows.h>
#include <asio2/websocket/ws_server.hpp>
#include <gtest/gtest.h>

#include "connection/sdk_websocket_reconnect.h"
#include "connection/ws_connection.h"
#include "px_common_new/message_notifier.h"
#include "sdk_params.h"

namespace px {
namespace {

using namespace std::chrono_literals;

bool WaitUntil(const std::function<bool()>& predicate, const std::chrono::steady_clock::duration timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

std::shared_ptr<WsConnection> MakeConnection(
    const std::shared_ptr<MessageNotifier>& notifier,
    const int port) {
    return std::make_shared<WsConnection>(
        std::make_shared<ThunderSdkParams>(), notifier, "127.0.0.1", port, "/sdk-reconnect-test");
}

TEST(SdkWebSocketReconnect, InitiallyUnavailableServerIsRetriedUntilItStarts) {
    const auto port = 50000 + static_cast<int>(GetCurrentProcessId() % 5000);
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto connection = MakeConnection(notifier, port);
    const auto connected = std::make_shared<std::atomic_uint32_t>(0);
    connection->RegisterOnConnectedCallback([connected] {
        connected->fetch_add(1, std::memory_order_acq_rel);
    });
    connection->Start();

    ASSERT_TRUE(WaitUntil([connection] { return connection->ConnectionGeneration() >= 2; }, 6s));
    const auto generation_before_server_start = connection->ConnectionGeneration();
    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    ASSERT_TRUE(WaitUntil([connection, generation_before_server_start] {
        return connection->IsAlive() && connection->ConnectionGeneration() >= generation_before_server_start;
    }, 10s));
    EXPECT_EQ(connected->load(std::memory_order_acquire), 1U);

    connection->Stop();
    server->stop();
    notifier->Stop(MessageBusStopMode::kCancel);
    EXPECT_FALSE(connection->IsAlive());
}

TEST(SdkWebSocketReconnect, RealServerRestartsAdvanceEveryGeneration) {
    const auto port = 55000 + static_cast<int>(GetCurrentProcessId() % 5000);
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    const auto connection = MakeConnection(notifier, port);
    const auto connected = std::make_shared<std::atomic_uint32_t>(0);
    const auto disconnected = std::make_shared<std::atomic_uint32_t>(0);
    connection->RegisterOnConnectedCallback([connected] {
        connected->fetch_add(1, std::memory_order_acq_rel);
    });
    connection->RegisterOnDisConnectedCallback([disconnected] {
        disconnected->fetch_add(1, std::memory_order_acq_rel);
    });
    connection->Start();
    ASSERT_TRUE(WaitUntil([connection] { return connection->IsAlive(); }, 5s));
    auto previous_generation = connection->ConnectionGeneration();

    for (int cycle = 0; cycle < 3; ++cycle) {
        server->stop();
        ASSERT_TRUE(WaitUntil([connection] { return !connection->IsAlive(); }, 3s)) << "cycle=" << cycle;
        ASSERT_TRUE(server->start("127.0.0.1", port)) << "cycle=" << cycle;
        ASSERT_TRUE(WaitUntil([connection, previous_generation] {
            return connection->IsAlive() && connection->ConnectionGeneration() > previous_generation;
        }, 10s)) << "cycle=" << cycle;
        previous_generation = connection->ConnectionGeneration();
    }
    EXPECT_EQ(connected->load(std::memory_order_acquire), 4U);
    EXPECT_EQ(disconnected->load(std::memory_order_acquire), 3U);

    connection->Stop();
    server->stop();
    notifier->Stop(MessageBusStopMode::kCancel);
}

TEST(SdkWebSocketReconnect, RepeatedStartStopLeavesNoLiveAdapter) {
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto connection = MakeConnection(notifier, 9);
    for (int cycle = 0; cycle < 10; ++cycle) {
        connection->Start();
        connection->Stop();
        EXPECT_FALSE(connection->IsAlive()) << "cycle=" << cycle;
    }
    notifier->Stop(MessageBusStopMode::kCancel);
}

TEST(SdkWebSocketReconnect, StopFromReadyCallbackDrainsAndAllowsRestart) {
    const auto port = 52000 + static_cast<int>(GetCurrentProcessId() % 5000);
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    const auto connection = MakeConnection(notifier, port);
    const std::weak_ptr<WsConnection> weak_connection = connection;
    const auto connected = std::make_shared<std::atomic_uint32_t>(0);
    connection->RegisterOnConnectedCallback([weak_connection, connected] {
        if (connected->fetch_add(1, std::memory_order_acq_rel) == 0) {
            if (const auto current = weak_connection.lock()) {
                current->Stop();
            }
        }
    });

    connection->Start();
    ASSERT_TRUE(WaitUntil([connected] { return connected->load(std::memory_order_acquire) == 1; }, 5s));
    ASSERT_TRUE(WaitUntil([connection, connected] {
        connection->Start();
        return connected->load(std::memory_order_acquire) >= 2 && connection->IsAlive();
    }, 10s));

    connection->Stop();
    server->stop();
    notifier->Stop(MessageBusStopMode::kCancel);
}

TEST(SdkWebSocketReconnect, SessionRejectionIsTerminal) {
    for (const auto rejection : {
             WsControlRejection::kAuthorization,
             WsControlRejection::kOccupied,
             WsControlRejection::kSessionPolicy}) {
        const auto error = MakeSdkWebSocketRejectionError(rejection);
        EXPECT_FALSE(error.retryable);
        EXPECT_EQ(error.StableCode(), "SDK_WEBSOCKET_SESSION_REJECTED");
    }
}

} // namespace
} // namespace px
