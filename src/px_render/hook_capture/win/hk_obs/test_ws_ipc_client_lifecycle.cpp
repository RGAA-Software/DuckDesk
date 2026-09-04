#include <chrono>
#include <future>
#include <functional>
#include <memory>
#include <thread>

#include <asio2/websocket/ws_server.hpp>
#include <gtest/gtest.h>
#include <Windows.h>

#include "ws_ipc_client.h"

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

TEST(WsIpcClientLifecycle, RepeatedStartStopRecreatesPrivateRuntime) {
    const auto client = WsIpcClient::Make(9);
    ASSERT_TRUE(client);

    for (int cycle = 0; cycle < 20; ++cycle) {
        client->Start();
        EXPECT_TRUE(client->IsStarted()) << "cycle=" << cycle;
        client->Exit();
        EXPECT_FALSE(client->IsStarted()) << "cycle=" << cycle;
    }
}

TEST(WsIpcClientLifecycle, StopAsyncDrainsFromExternalControlLane) {
    const auto client = WsIpcClient::Make(9);
    ASSERT_TRUE(client);
    client->Start();
    ASSERT_TRUE(client->IsStarted());

    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    ASSERT_TRUE(scope);
    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("test-obs-ipc-stop", [client, completion]() -> PxAwaitable<void> {
        completion->set_value(co_await WsIpcClient::StopAsync(
            client, std::chrono::steady_clock::now() + 5s));
        co_return;
    }));

    ASSERT_EQ(future.wait_for(6s), std::future_status::ready);
    const auto stopped = future.get();
    if (!stopped) {
        ADD_FAILURE() << stopped.Error().StableCode() << ": " << stopped.Error().message;
    }
    EXPECT_FALSE(client->IsStarted());

    scope->BeginStop();
    EXPECT_TRUE(scope->WaitFor(1s));
    runtime->RequestDrain();
    runtime->Join();
}

TEST(WsIpcClientLifecycle, RealServerDisconnectAdvancesReconnectGeneration) {
    const auto port = 30000 + static_cast<int>(GetCurrentProcessId() % 10000);
    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    const auto client = WsIpcClient::Make(port);
    client->Start();
    ASSERT_TRUE(WaitUntil([client] { return client->IsConnected(); }, 5s));
    const auto first_generation = client->ConnectionGeneration();
    ASSERT_GT(first_generation, 0U);

    server->stop();
    ASSERT_TRUE(WaitUntil([client] { return !client->IsConnected(); }, 2s));
    ASSERT_TRUE(server->start("127.0.0.1", port));
    ASSERT_TRUE(WaitUntil([client, first_generation] {
        return client->IsConnected() && client->ConnectionGeneration() > first_generation;
    }, 8s));

    client->Exit();
    server->stop();
    EXPECT_FALSE(client->IsStarted());
}

} // namespace
} // namespace px
