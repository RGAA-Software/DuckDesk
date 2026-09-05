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

PxAwaitable<void> StopClientAndComplete(std::shared_ptr<WsIpcClient> client, std::shared_ptr<std::promise<PxResult<void>>> completion) {
    completion->set_value(co_await WsIpcClient::StopAsync(client, std::chrono::steady_clock::now() + 5s));
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
    ASSERT_TRUE(scope->Spawn("test-obs-ipc-stop", [client, completion] { return StopClientAndComplete(client, completion); }));

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

TEST(WsIpcClientLifecycle, ConnectionQueriesRemainSafeDuringStop) {
    const auto client = WsIpcClient::Make(9);
    ASSERT_TRUE(client);
    client->Start();
    ASSERT_TRUE(client->IsStarted());

    std::jthread query_thread([client](const std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            static_cast<void>(client->IsStarted());
            static_cast<void>(client->IsConnected());
            static_cast<void>(client->ConnectionGeneration());
        }
    });
    client->Exit();
    query_thread.request_stop();
    query_thread.join();
    EXPECT_FALSE(client->IsStarted());
    EXPECT_FALSE(client->IsConnected());
    EXPECT_EQ(client->ConnectionGeneration(), 0U);
}

TEST(WsIpcClientLifecycle, RepeatedRealServerRestartAdvancesEveryReconnectGeneration) {
    const auto port = 30000 + static_cast<int>(GetCurrentProcessId() % 10000);
    auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    const auto client = WsIpcClient::Make(port);
    client->Start();
    ASSERT_TRUE(WaitUntil([client] { return client->IsConnected(); }, 5s));
    auto previous_generation = client->ConnectionGeneration();
    ASSERT_GT(previous_generation, 0U);

    for (int cycle = 0; cycle < 3; ++cycle) {
        server->stop();
        ASSERT_TRUE(WaitUntil([client] { return !client->IsConnected(); }, 2s)) << "cycle=" << cycle;
        server.reset();
        server = std::make_shared<asio2::ws_server>();
        ASSERT_TRUE(server->start("127.0.0.1", port)) << "cycle=" << cycle;
        ASSERT_TRUE(
            WaitUntil([client, previous_generation] { return client->IsConnected() && client->ConnectionGeneration() > previous_generation; }, 8s))
            << "cycle=" << cycle;
        previous_generation = client->ConnectionGeneration();
    }

    client->Exit();
    server->stop();
    EXPECT_FALSE(client->IsStarted());
}

TEST(WsIpcClientLifecycle, InitiallyUnavailableServerIsRetriedUntilItStarts) {
    const auto port = 40000 + static_cast<int>(GetCurrentProcessId() % 10000);
    const auto client = WsIpcClient::Make(port);
    client->Start();
    ASSERT_TRUE(client->IsStarted());

    ASSERT_TRUE(WaitUntil([client] { return client->ConnectionGeneration() >= 2; }, 5s));
    const auto generation_before_server_start = client->ConnectionGeneration();
    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    ASSERT_TRUE(WaitUntil(
        [client, generation_before_server_start] {
            return client->IsConnected() && client->ConnectionGeneration() >= generation_before_server_start;
        },
        10s));

    client->Exit();
    server->stop();
    EXPECT_FALSE(client->IsStarted());
}

} // namespace
} // namespace px
