#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include <Windows.h>
#include <gtest/gtest.h>

#include "px_common_new/async_runtime.h"
#include "px_render/network/relay/relay_transport_runtime.h"

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

TEST(RelayTransportReconnectOwner, OuterRuntimeKeepsOneSdkWhileInnerSupervisorRetries) {
    const auto async_runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(async_runtime);
    ASSERT_TRUE(async_runtime->Start());
    const auto unavailable_port = 61000 + static_cast<int>(GetCurrentProcessId() % 3000);
    RenderModuleSettings settings{};
    settings.device_id = "relay-reconnect-owner-test";
    settings.relay_enabled = true;
    settings.relay_host = "127.0.0.1";
    settings.relay_port = std::to_string(unavailable_port);
    settings.appkey = "relay-reconnect-owner-appkey";
    const auto runtime = RelayTransportRuntime::Create(RelayTransportRuntimeConfig{
        .relay_device_id = settings.device_id,
        .configured_host = settings.relay_host,
        .configured_port = unavailable_port,
        .settings = settings,
        .async_runtime = async_runtime,
    });
    ASSERT_TRUE(runtime);
    runtime->Start({}, {});

    ASSERT_TRUE(WaitUntil([runtime] {
        return runtime->MediaChannelInstanceGeneration() == 1 && runtime->MediaConnectionAttemptGeneration() >= 3;
    }, 8s));
    const auto instance_generation = runtime->MediaChannelInstanceGeneration();
    const auto attempt_generation = runtime->MediaConnectionAttemptGeneration();
    std::this_thread::sleep_for(3s);
    EXPECT_EQ(runtime->MediaChannelInstanceGeneration(), instance_generation);
    EXPECT_GE(runtime->MediaConnectionAttemptGeneration(), attempt_generation);

    runtime->Stop();
    async_runtime->RequestDrain();
    async_runtime->Join();
}

TEST(RelayTransportReconnectOwner, OwnerExpiryStopsMonitorWithoutExplicitStop) {
    const auto async_runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(async_runtime);
    ASSERT_TRUE(async_runtime->Start());
    const auto unavailable_port = 62000 + static_cast<int>(GetCurrentProcessId() % 3000);
    RenderModuleSettings settings{};
    settings.device_id = "relay-owner-expiry-test";
    settings.relay_enabled = true;
    settings.relay_host = "127.0.0.1";
    settings.relay_port = std::to_string(unavailable_port);
    settings.appkey = "relay-owner-expiry-appkey";

    std::weak_ptr<RelayTransportRuntime> weak_runtime{};
    {
        const auto runtime = RelayTransportRuntime::Create(RelayTransportRuntimeConfig{
            .relay_device_id = settings.device_id,
            .configured_host = settings.relay_host,
            .configured_port = unavailable_port,
            .settings = settings,
            .async_runtime = async_runtime,
        });
        ASSERT_TRUE(runtime);
        weak_runtime = runtime;
        runtime->Start({}, {});
        ASSERT_TRUE(WaitUntil([runtime]() { return runtime->MediaChannelInstanceGeneration() == 1; }, 5s));
    }

    EXPECT_TRUE(WaitUntil([weak_runtime]() { return weak_runtime.expired(); }, 5s));
    async_runtime->RequestDrain();
    async_runtime->Join();
}

} // namespace
} // namespace px
