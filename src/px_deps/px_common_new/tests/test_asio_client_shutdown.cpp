#include <atomic>
#include <chrono>
#include <future>
#include <functional>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/async_runtime.h"

namespace px {
namespace {

using namespace std::chrono_literals;

class FakeAsioClient final : public std::enable_shared_from_this<FakeAsioClient> {
public:
    void post(std::function<void()> task) {
        task_ = std::jthread([task = std::move(task)] { task(); });
    }

    void set_auto_reconnect(bool) {}
    void stop_all_timers() {}

    void stop() {
        std::this_thread::sleep_for(20ms);
        stopped_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool is_stopped() const {
        return stopped_.load(std::memory_order_acquire);
    }

private:
    std::atomic_bool stopped_{false};
    std::jthread task_{};
};

PxAwaitable<void> CollectClientStop(
    std::shared_ptr<FakeAsioClient> client,
    std::chrono::steady_clock::time_point deadline,
    std::shared_ptr<std::promise<PxResult<void>>> completion) {
    completion->set_value(co_await WaitForAsioClientStopped(client, deadline, "asio-client-stop.test"));
}

TEST(AsioClientShutdown, RequestAndAwaitRequireAdapterQuiescence) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto client = std::make_shared<FakeAsioClient>();
    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(RequestAsioClientStop(client, "asio-client-stop.request"));
    ASSERT_TRUE(scope->Spawn("wait-client-stop", [client, completion] {
        return CollectClientStop(client, std::chrono::steady_clock::now() + 1s, completion);
    }));
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(future.get());
    EXPECT_TRUE(IsAsioClientStopped(client));
    EXPECT_TRUE(scope->WaitFor(1s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(AsioClientShutdown, BlockingFacadeUsesSameTerminalCondition) {
    const auto client = std::make_shared<FakeAsioClient>();
    ASSERT_TRUE(RequestAsioClientStop(client, "asio-client-stop.request"));
    EXPECT_TRUE(WaitForAsioClientStoppedBlocking(client, std::chrono::steady_clock::now() + 1s));
}

} // namespace
} // namespace px
