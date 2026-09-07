#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

#include <asio2/external/asio.hpp>

#include "render_panel/console_scanner/console_datagram_receiver.h"

namespace px {
namespace {

using namespace std::chrono_literals;
using Udp = asio::ip::udp;

class DatagramSink final {
public:
    void Add(std::string message) {
        {
            std::lock_guard lock(mutex_);
            messages_.push_back(std::move(message));
        }
        condition_.notify_all();
    }

    bool WaitForCount(std::size_t count, std::chrono::milliseconds timeout = 2s) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [&messages = messages_, count]() {
            return messages.size() >= count;
        });
    }

    [[nodiscard]] std::vector<std::string> Messages() const {
        std::lock_guard lock(mutex_);
        return messages_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<std::string> messages_;
};

std::shared_ptr<PxAsyncRuntime> StartRuntime() {
    const auto runtime = PxAsyncRuntime::Create(PxAsyncRuntimeOptions {.worker_threads = 2});
    return runtime && runtime->Start() ? runtime : nullptr;
}

std::uint16_t WaitForBoundPort(
    const std::shared_ptr<ConsoleDatagramReceiver>& receiver,
    std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto port = receiver ? receiver->BoundPort() : 0;
        if (port != 0) {
            return port;
        }
        std::this_thread::sleep_for(5ms);
    }
    return 0;
}

bool SendDatagram(std::uint16_t port, const std::string& payload) {
    asio::io_context context;
    Udp::socket socket(context);
    asio::error_code error;
    socket.open(Udp::v4(), error);
    if (error) {
        return false;
    }
    const auto sent = socket.send_to(
        asio::buffer(payload), Udp::endpoint(asio::ip::address_v4::loopback(), port), 0, error);
    return !error && sent == payload.size();
}

} // namespace

TEST(ConsoleDatagramReceiver, ReceivesLoopbackDatagram) {
    const auto runtime = StartRuntime();
    ASSERT_TRUE(runtime);
    const auto receiver = ConsoleDatagramReceiver::Create(runtime, 25ms);
    const auto sink = std::make_shared<DatagramSink>();
    ASSERT_TRUE(receiver);
    ASSERT_TRUE(receiver->Start(0, [sink](std::string message) {
        sink->Add(std::move(message));
    }));

    const auto port = WaitForBoundPort(receiver);
    ASSERT_NE(port, 0);
    ASSERT_TRUE(SendDatagram(port, "console://access/test"));
    ASSERT_TRUE(sink->WaitForCount(1));
    EXPECT_EQ(sink->Messages(), std::vector<std::string>({"console://access/test"}));

    receiver->Stop();
    EXPECT_EQ(receiver->Statistics().outstanding, 0);
}

TEST(ConsoleDatagramReceiver, StopCancelsPendingReceivePromptly) {
    const auto runtime = StartRuntime();
    ASSERT_TRUE(runtime);
    const auto receiver = ConsoleDatagramReceiver::Create(runtime, 25ms);
    ASSERT_TRUE(receiver);
    ASSERT_TRUE(receiver->Start(0, [](std::string) {}));
    ASSERT_NE(WaitForBoundPort(receiver), 0);

    const auto begin = std::chrono::steady_clock::now();
    receiver->Stop();
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    EXPECT_LT(elapsed, 1s);
    EXPECT_EQ(receiver->Statistics().outstanding, 0);
}

TEST(ConsoleDatagramReceiver, DestructionCancelsPendingReceiveAndReleasesPort) {
    const auto runtime = StartRuntime();
    ASSERT_TRUE(runtime);
    auto receiver = ConsoleDatagramReceiver::Create(runtime, 25ms);
    ASSERT_TRUE(receiver);
    ASSERT_TRUE(receiver->Start(0, [](std::string) {}));
    const auto port = WaitForBoundPort(receiver);
    ASSERT_NE(port, 0);

    const std::weak_ptr<ConsoleDatagramReceiver> weak_receiver = receiver;
    receiver.reset();
    EXPECT_TRUE(weak_receiver.expired());

    asio::io_context context;
    asio::error_code error;
    Udp::socket replacement(context);
    replacement.open(Udp::v4(), error);
    ASSERT_FALSE(error);
    replacement.bind(Udp::endpoint(Udp::v4(), port), error);
    EXPECT_FALSE(error) << error.message();
}

TEST(ConsoleDatagramReceiver, RejectsDuplicateStart) {
    const auto runtime = StartRuntime();
    ASSERT_TRUE(runtime);
    const auto receiver = ConsoleDatagramReceiver::Create(runtime, 25ms);
    ASSERT_TRUE(receiver);
    ASSERT_TRUE(receiver->Start(0, [](std::string) {}));
    EXPECT_FALSE(receiver->Start(0, [](std::string) {}));
    receiver->Stop();
}

TEST(ConsoleDatagramReceiver, StopFromHandlerDoesNotSelfWait) {
    const auto runtime = StartRuntime();
    ASSERT_TRUE(runtime);
    const auto receiver = ConsoleDatagramReceiver::Create(runtime, 25ms);
    const auto sink = std::make_shared<DatagramSink>();
    const std::weak_ptr<ConsoleDatagramReceiver> weak_receiver = receiver;
    ASSERT_TRUE(receiver);
    ASSERT_TRUE(receiver->Start(0, [weak_receiver, sink](std::string message) {
        sink->Add(std::move(message));
        if (const auto locked = weak_receiver.lock()) {
            locked->Stop();
        }
    }));

    const auto port = WaitForBoundPort(receiver);
    ASSERT_NE(port, 0);
    ASSERT_TRUE(SendDatagram(port, "stop-from-handler"));
    ASSERT_TRUE(sink->WaitForCount(1));
    receiver->Stop();
    EXPECT_EQ(receiver->Statistics().outstanding, 0);
}

TEST(ConsoleDatagramReceiver, RetriesAfterPortBecomesAvailable) {
    asio::io_context occupied_context;
    Udp::socket occupied(occupied_context, Udp::endpoint(Udp::v4(), 0));
    const auto port = occupied.local_endpoint().port();

    const auto runtime = StartRuntime();
    ASSERT_TRUE(runtime);
    const auto receiver = ConsoleDatagramReceiver::Create(runtime, 25ms);
    const auto sink = std::make_shared<DatagramSink>();
    ASSERT_TRUE(receiver);
    ASSERT_TRUE(receiver->Start(port, [sink](std::string message) {
        sink->Add(std::move(message));
    }));
    std::this_thread::sleep_for(75ms);
    EXPECT_EQ(receiver->BoundPort(), 0);

    occupied.close();
    ASSERT_EQ(WaitForBoundPort(receiver), port);
    ASSERT_TRUE(SendDatagram(port, "recovered"));
    ASSERT_TRUE(sink->WaitForCount(1));
    EXPECT_EQ(sink->Messages(), std::vector<std::string>({"recovered"}));
    receiver->Stop();
}

TEST(ConsoleDatagramReceiver, StopCancelsPendingRetryTimerPromptly) {
    asio::io_context occupied_context;
    Udp::socket occupied(occupied_context, Udp::endpoint(Udp::v4(), 0));
    const auto port = occupied.local_endpoint().port();

    const auto runtime = StartRuntime();
    ASSERT_TRUE(runtime);
    const auto receiver = ConsoleDatagramReceiver::Create(runtime, 2s);
    ASSERT_TRUE(receiver);
    ASSERT_TRUE(receiver->Start(port, [](std::string) {}));
    std::this_thread::sleep_for(50ms);

    const auto begin = std::chrono::steady_clock::now();
    receiver->Stop();
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    EXPECT_LT(elapsed, 1s);
    EXPECT_EQ(receiver->Statistics().outstanding, 0);
}

TEST(ConsoleDatagramReceiver, HandlerExceptionDoesNotStopReceiver) {
    const auto runtime = StartRuntime();
    ASSERT_TRUE(runtime);
    const auto receiver = ConsoleDatagramReceiver::Create(runtime, 25ms);
    const auto sink = std::make_shared<DatagramSink>();
    const auto calls = std::make_shared<std::atomic_int>(0);
    ASSERT_TRUE(receiver);
    ASSERT_TRUE(receiver->Start(0, [calls, sink](std::string message) {
        if (calls->fetch_add(1) == 0) {
            throw std::runtime_error("expected test failure");
        }
        sink->Add(std::move(message));
    }));

    const auto port = WaitForBoundPort(receiver);
    ASSERT_NE(port, 0);
    ASSERT_TRUE(SendDatagram(port, "throws"));
    ASSERT_TRUE(SendDatagram(port, "survives"));
    ASSERT_TRUE(sink->WaitForCount(1));
    EXPECT_EQ(sink->Messages(), std::vector<std::string>({"survives"}));
    receiver->Stop();
}

TEST(ConsoleDatagramReceiver, TenRepeatedLifecyclesCompleteCleanly) {
    const auto runtime = StartRuntime();
    ASSERT_TRUE(runtime);
    for (int round = 0; round < 10; ++round) {
        const auto receiver = ConsoleDatagramReceiver::Create(runtime, 25ms);
        const auto sink = std::make_shared<DatagramSink>();
        ASSERT_TRUE(receiver) << "round=" << round;
        ASSERT_TRUE(receiver->Start(0, [sink](std::string message) {
            sink->Add(std::move(message));
        })) << "round=" << round;
        const auto port = WaitForBoundPort(receiver);
        ASSERT_NE(port, 0) << "round=" << round;
        ASSERT_TRUE(SendDatagram(port, "round-" + std::to_string(round))) << "round=" << round;
        ASSERT_TRUE(sink->WaitForCount(1)) << "round=" << round;
        receiver->Stop();
        EXPECT_EQ(receiver->Statistics().outstanding, 0) << "round=" << round;
    }
}

} // namespace px
