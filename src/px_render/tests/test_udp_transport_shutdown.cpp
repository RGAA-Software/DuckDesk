#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <asio2/external/asio.hpp>
#include <gtest/gtest.h>
#include <Windows.h>

#include "network/udp/udp_transport.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxAwaitable<void> CollectUdpStop(
    const std::shared_ptr<UdpTransport>& transport,
    const std::chrono::steady_clock::time_point deadline,
    const std::shared_ptr<std::promise<PxResult<void>>>& completion) {
    completion->set_value(co_await UdpTransport::StopAsync(transport, deadline));
}

TEST(UdpTransportShutdown, ReceiveStormDrainsBeforeAbsoluteDeadline) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto shutdown_scope = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
    ASSERT_TRUE(shutdown_scope);
    const auto transport = std::make_shared<UdpTransport>(runtime);
    RenderModuleConfiguration configuration{};
    configuration.udp_listen_port = 40000 + static_cast<std::int64_t>(GetCurrentProcessId() % 10000);
    ASSERT_TRUE(transport->Start(configuration));

    const auto sent = std::make_shared<std::atomic_uint64_t>(0);
    const auto payload = std::make_shared<const std::string>(1200, 'u');
    const auto endpoint = asio::ip::udp::endpoint(
        asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(configuration.udp_listen_port));
    std::jthread sender([sent, payload, endpoint](const std::stop_token stop_token) {
        asio::io_context io_context;
        asio::ip::udp::socket socket(io_context);
        asio::error_code error;
        socket.open(asio::ip::udp::v4(), error);
        if (error) {
            return;
        }
        while (!stop_token.stop_requested()) {
            socket.send_to(asio::buffer(*payload), endpoint, 0, error);
            if (error) {
                error.clear();
                continue;
            }
            sent->fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::this_thread::sleep_for(50ms);

    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();
    const auto stop_started = std::chrono::steady_clock::now();
    ASSERT_TRUE(shutdown_scope->Spawn("test-udp-stop", [transport, completion]() {
        return CollectUdpStop(transport, std::chrono::steady_clock::now() + 3s, completion);
    }));
    ASSERT_EQ(future.wait_for(4s), std::future_status::ready);
    const auto stopped = future.get();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
    sender.request_stop();
    sender.join();

    if (!stopped) {
        ADD_FAILURE() << stopped.Error().StableCode() << ": " << stopped.Error().message;
    }
    EXPECT_GT(sent->load(std::memory_order_relaxed), 0U);
    EXPECT_LT(stop_elapsed, 3s);
    EXPECT_FALSE(transport->IsWorking());
    RecordProperty("storm_packets_sent", sent->load(std::memory_order_relaxed));
    RecordProperty("stop_latency_us", std::chrono::duration_cast<std::chrono::microseconds>(stop_elapsed).count());

    shutdown_scope->BeginStop();
    EXPECT_TRUE(shutdown_scope->WaitFor(1s));
    runtime->RequestDrain();
    runtime->Join();
}

} // namespace
} // namespace px
