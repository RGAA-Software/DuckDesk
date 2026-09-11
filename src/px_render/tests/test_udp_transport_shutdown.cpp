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
#include "network/udp/windows_udp_batch.h"
#include "px_common/data.h"
#include "px_common/px_udp_protocol.h"
#include "px_common/time_util.h"

namespace px {
namespace {

using namespace std::chrono_literals;

TEST(WindowsUdpBatch, SegmentsOrFallsBackWithoutDuplicateDatagrams) {
    asio::io_context io{};
    asio::ip::udp::socket receiver(io, asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
    asio::ip::udp::socket sender(io, asio::ip::udp::v4());
    receiver.non_blocking(true);
    sender.non_blocking(true);
    std::vector<media::Packet> packets{};
    for (std::uint8_t index{}; index < 4; ++index)
        packets.emplace_back(400, index);
    const auto result = TryWindowsUdpBatch(sender, receiver.local_endpoint(), packets);
    ASSERT_NE(result, UdpBatchResult::kIncomplete);
    if (result == UdpBatchResult::kFallback) {
        for (const auto& packet : packets)
            ASSERT_EQ(sender.send_to(asio::buffer(packet), receiver.local_endpoint()), packet.size());
    }
    RecordProperty("segmentation_supported", result == UdpBatchResult::kSent);
    for (const auto& expected : packets) {
        media::Packet actual(2000);
        asio::error_code error{};
        std::size_t bytes{};
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        do {
            bytes = receiver.receive(asio::buffer(actual), 0, error);
            if (!error || std::chrono::steady_clock::now() >= deadline)
                break;
            std::this_thread::sleep_for(1ms);
        } while (error == asio::error::would_block || error == asio::error::try_again);
        ASSERT_FALSE(error) << error.message();
        actual.resize(bytes);
        EXPECT_EQ(actual, expected);
    }
    media::Packet extra(2000);
    asio::error_code error{};
    receiver.receive(asio::buffer(extra), 0, error);
    EXPECT_TRUE(error == asio::error::would_block || error == asio::error::try_again);
}

TEST(WindowsUdpBatch, InvalidBatchAndClosedSocketUseFallback) {
    asio::io_context io{};
    asio::ip::udp::socket socket(io, asio::ip::udp::v4());
    const asio::ip::udp::endpoint target(asio::ip::address_v4::loopback(), 9);
    EXPECT_EQ(TryWindowsUdpBatch(socket, target, {}), UdpBatchResult::kFallback);
    const std::vector<media::Packet> unequal{media::Packet(400), media::Packet(401)};
    EXPECT_EQ(TryWindowsUdpBatch(socket, target, unequal), UdpBatchResult::kFallback);
    const std::vector<media::Packet> oversized(2, media::Packet(40000));
    EXPECT_EQ(TryWindowsUdpBatch(socket, target, oversized), UdpBatchResult::kFallback);
    socket.close();
    const std::vector<media::Packet> valid(2, media::Packet(400));
    EXPECT_EQ(TryWindowsUdpBatch(socket, target, valid), UdpBatchResult::kFallback);
}

PxAwaitable<void> CollectUdpStop(std::shared_ptr<UdpTransport> transport, const std::chrono::steady_clock::time_point deadline,
                                 std::shared_ptr<std::promise<PxResult<void>>> completion) {
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
    configuration.async_runtime = runtime;
    configuration.udp_listen_port = 40000 + static_cast<std::int64_t>(GetCurrentProcessId() % 10000);
    ASSERT_TRUE(transport->Start(configuration));

    const auto sent = std::make_shared<std::atomic_uint64_t>(0);
    const auto payload = std::make_shared<const std::string>(1200, 'u');
    const auto endpoint = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(configuration.udp_listen_port));
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
    ASSERT_TRUE(shutdown_scope->Spawn(
        "test-udp-stop", [transport, completion]() { return CollectUdpStop(transport, std::chrono::steady_clock::now() + 3s, completion); }));
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

TEST(UdpTransportShutdown, AuthorizedPacedVideoCancelsWithoutWaitingForTheWholeFrame) {
    for (unsigned iteration{}; iteration < 3; ++iteration) {
        const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
        ASSERT_TRUE(runtime && runtime->Start());
        const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
        const auto transport = std::make_shared<UdpTransport>(runtime);
        RenderModuleConfiguration configuration{};
        configuration.async_runtime = runtime;
        configuration.udp_listen_port = 51000 + GetCurrentProcessId() % 1000 + iteration;
        configuration.udp_media_budget_bps = 1'000'000;
        ASSERT_TRUE(transport->Start(configuration));
        transport->UpdateUdpMediaAssociation({.association_code_ = "paced-stop",
                                              .logical_session_id_ = "paced-stop",
                                              .stream_id_ = "test",
                                              .expires_at_ms_ = static_cast<std::int64_t>(TimeUtil::GetCurrentTimestamp()) + 10000});
        asio::io_context io{};
        asio::ip::udp::socket socket(io, asio::ip::udp::v4());
        const auto hello = PxUdpProtocol::BuildHello("paced-stop", "test");
        const asio::ip::udp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(configuration.udp_listen_port));
        socket.send_to(asio::buffer(hello->Bytes()), endpoint);
        for (unsigned retry{}; retry < 100 && !transport->IsWorking(); ++retry)
            std::this_thread::sleep_for(5ms);
        ASSERT_TRUE(transport->IsWorking());
        const std::string bytes(1'000'000, 'v');
        const auto data = Data::Copy(std::span<const char>{bytes});
        const auto entered = std::make_shared<std::promise<void>>();
        auto entered_future = entered->get_future();
        std::jthread sender([transport, data, entered]() {
            entered->set_value();
            transport->SubmitEncodedVideo("test", EncodedVideoType::kH264, data, 1, 1280, 720, true, EncodedReferenceState::kDependent);
        });
        entered_future.wait();
        std::this_thread::sleep_for(20ms);
        const auto completion = std::make_shared<std::promise<PxResult<void>>>();
        auto future = completion->get_future();
        const auto started = std::chrono::steady_clock::now();
        ASSERT_TRUE(scope->Spawn("paced-stop",
                                 [transport, completion]() { return CollectUdpStop(transport, std::chrono::steady_clock::now() + 2s, completion); }));
        ASSERT_EQ(future.wait_for(3s), std::future_status::ready);
        EXPECT_TRUE(future.get());
        sender.join();
        EXPECT_LT(std::chrono::steady_clock::now() - started, 1s);
        EXPECT_FALSE(transport->IsWorking());
        transport->Destroy();
        scope->BeginStop();
        EXPECT_TRUE(scope->WaitFor(1s));
        runtime->RequestDrain();
        runtime->Join();
    }
}

} // namespace
} // namespace px
