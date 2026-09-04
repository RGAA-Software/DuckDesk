#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include <Windows.h>
#include <asio2/websocket/ws_server.hpp>
#include <gtest/gtest.h>

#include "px_common_new/async_runtime.h"
#include "px_relay_client/relay_ws_client.h"

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

std::shared_ptr<RelayWsClient> MakeClient(const std::shared_ptr<PxAsyncRuntime>& runtime, const int port) {
    return std::make_shared<RelayWsClient>(
        "127.0.0.1", port, "server-test", "test", "stream-test", "appkey-test", false, "", "", "", runtime);
}

TEST(RelayWsReconnect, InitiallyUnavailableAndRepeatedServerRestartRecover) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto port = 60000 + static_cast<int>(GetCurrentProcessId() % 4000);
    const auto client = MakeClient(runtime, port);
    const auto connected = std::make_shared<std::atomic_uint32_t>(0);
    const auto disconnected = std::make_shared<std::atomic_uint32_t>(0);
    client->SetOnRelayServerConnectedCallback([connected] {
        connected->fetch_add(1, std::memory_order_acq_rel);
    });
    client->SetOnRelayServerDisConnectedCallback([disconnected] {
        disconnected->fetch_add(1, std::memory_order_acq_rel);
    });
    client->Start();
    ASSERT_TRUE(WaitUntil([client] { return client->ConnectionGeneration() >= 2; }, 6s));

    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    ASSERT_TRUE(WaitUntil([client] { return client->IsAlive(); }, 10s));
    auto previous_generation = client->ConnectionGeneration();
    for (int cycle = 0; cycle < 2; ++cycle) {
        server->stop();
        ASSERT_TRUE(WaitUntil([client] { return !client->IsAlive(); }, 3s));
        ASSERT_TRUE(server->start("127.0.0.1", port));
        ASSERT_TRUE(WaitUntil([client, previous_generation] {
            return client->IsAlive() && client->ConnectionGeneration() > previous_generation;
        }, 10s));
        previous_generation = client->ConnectionGeneration();
    }
    EXPECT_EQ(connected->load(std::memory_order_acquire), 3U);
    EXPECT_EQ(disconnected->load(std::memory_order_acquire), 2U);

    client->Stop();
    server->stop();
    runtime->RequestDrain();
    runtime->Join();
}

TEST(RelayWsReconnect, StopFromReadyCallbackDrainsAndAllowsRestart) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto port = 62000 + static_cast<int>(GetCurrentProcessId() % 3000);
    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    const auto client = MakeClient(runtime, port);
    const std::weak_ptr<RelayWsClient> weak_client = client;
    const auto connected = std::make_shared<std::atomic_uint32_t>(0);
    client->SetOnRelayServerConnectedCallback([weak_client, connected] {
        if (connected->fetch_add(1, std::memory_order_acq_rel) == 0) {
            if (const auto current = weak_client.lock()) {
                current->Stop();
            }
        }
    });

    client->Start();
    ASSERT_TRUE(WaitUntil([connected] { return connected->load(std::memory_order_acquire) == 1; }, 5s));
    ASSERT_TRUE(WaitUntil([client, connected] {
        client->Start();
        return connected->load(std::memory_order_acquire) >= 2 && client->IsAlive();
    }, 10s));

    client->Stop();
    server->stop();
    runtime->RequestDrain();
    runtime->Join();
}

} // namespace
} // namespace px
