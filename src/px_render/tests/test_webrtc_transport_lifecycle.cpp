#include <Windows.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "px_render/network/webrtc/webrtc_execution_context.h"
#include "px_render/network/webrtc_transport_host.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxAwaitable<void> CollectWebRtcStop(std::shared_ptr<WebRtcTransportHandle> transport, std::shared_ptr<std::promise<PxResult<void>>> completion) {
    completion->set_value(co_await WebRtcTransportHandle::StopAsync(transport, std::chrono::steady_clock::now() + 5s));
}

void RunLifecycleRounds(const std::filesystem::path& rtc_local_path) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    ASSERT_TRUE(scope);
    for (int round = 0; round < 100; ++round) {
        auto host = WebRtcTransportHost::Create();
        auto transports = host->CreateTransports();
        ASSERT_EQ(transports.size(), 1U) << "round " << round;

        const WebRtcTransportConfiguration configuration{
            .async_runtime = runtime,
            .base_path = rtc_local_path.parent_path().generic_string(),
            .base_data_path = std::filesystem::temp_directory_path().wstring(),
            .device_id = "rtc-lifecycle-" + std::to_string(round),
            .language = 0,
            .appkey = "lifecycle-test-key",
        };
        for (const auto& transport : transports) {
            ASSERT_TRUE(transport->Start(configuration)) << "round " << round;
            transport->SetEventCallback([](const WebRtcEvent&) {});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        const auto stop_started = std::chrono::steady_clock::now();
        for (const auto& transport : transports) {
            const auto completion = std::make_shared<std::promise<PxResult<void>>>();
            auto future = completion->get_future();
            ASSERT_TRUE(scope->Spawn("test-webrtc-stop", [transport, completion] { return CollectWebRtcStop(transport, completion); }));
            ASSERT_EQ(future.wait_for(6s), std::future_status::ready);
            const auto stopped = future.get();
            if (!stopped) {
                ADD_FAILURE() << stopped.Error().StableCode() << ": " << stopped.Error().message;
            }
        }
        transports.clear();
        host->Reset();
        host.reset();
        EXPECT_LT(std::chrono::steady_clock::now() - stop_started, std::chrono::seconds(5)) << "round " << round;
        EXPECT_NE(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);
    }
    scope->BeginStop();
    EXPECT_TRUE(scope->WaitFor(1s));
    runtime->RequestDrain();
    runtime->Join();
}

TEST(WebRtcTransportLifecycle, RapidStartStopWithDirectlyLinkedDlls) { RunLifecycleRounds(PX_WEBRTC_LOCAL_LIBRARY_PATH); }

TEST(WebRtcTransportLifecycle, DllsRemainLoadedForProcessLifetime) {
    const auto rtc_local_path = std::filesystem::path(PX_WEBRTC_LOCAL_LIBRARY_PATH);
    auto host = WebRtcTransportHost::Create();
    auto transports = host->CreateTransports();
    ASSERT_EQ(transports.size(), 1U);
    EXPECT_EQ(transports[0]->Kind(), WebRtcTransportKind::kLocal);
    EXPECT_EQ(transports[0]->BaseName(), "px_render_rtc");

    host->Reset();
    host.reset();

    // Directly linked DLLs remain process dependencies after transport owners are released.
    EXPECT_NE(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);

    transports.clear();
    EXPECT_NE(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);
}

TEST(WebRtcTransportLifecycle, ExecutionContextCanStopFromItsEventCallback) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    auto context = WebRtcExecutionContext::Create(runtime, "webrtc-callback-stop-test");
    ASSERT_TRUE(context);
    const auto completion = std::make_shared<std::promise<void>>();
    auto future = completion->get_future();
    const auto weak_context = std::weak_ptr<WebRtcExecutionContext>(context);
    context->SetEventCallback([weak_context, completion](const WebRtcEvent&) {
        if (const auto locked = weak_context.lock()) {
            locked->BeginStop();
        }
        completion->set_value();
    });

    context->Publish(WebRtcInsertIdrEvent{});
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(context->StopAndWait(2s));
    context.reset();
    runtime->RequestDrain();
    runtime->Join();
}

TEST(WebRtcTransportLifecycle, ImmediateTerminalEventCanStopFromItsCallback) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    auto context = WebRtcExecutionContext::Create(runtime, "webrtc-immediate-terminal-test");
    ASSERT_TRUE(context);
    const auto delivered = std::make_shared<std::atomic_bool>(false);
    const auto weak_context = std::weak_ptr<WebRtcExecutionContext>(context);
    context->SetEventCallback([weak_context, delivered](const WebRtcEvent&) {
        delivered->store(true, std::memory_order_release);
        if (const auto locked = weak_context.lock()) {
            locked->BeginStop();
        }
    });

    context->Publish(WebRtcClientDisconnectedEvent{}, true);
    EXPECT_TRUE(delivered->load(std::memory_order_acquire));
    EXPECT_TRUE(context->StopAndWait(2s));
    context.reset();
    runtime->RequestDrain();
    runtime->Join();
}

TEST(WebRtcTransportLifecycle, TrafficEventPreservesConnectionAndByteDeltas) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    auto context = WebRtcExecutionContext::Create(runtime, "webrtc-traffic-event-test");
    ASSERT_TRUE(context);
    const auto completion = std::make_shared<std::promise<WebRtcTrafficEvent>>();
    auto future = completion->get_future();
    context->SetEventCallback([completion](const WebRtcEvent& event) {
        if (const auto traffic = std::get_if<WebRtcTrafficEvent>(&event)) {
            completion->set_value(*traffic);
        }
    });

    context->Publish(WebRtcTrafficEvent{
        .connection_id = "resource-connection",
        .sent_bytes = 4096,
        .received_bytes = 512,
    });
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto traffic = future.get();
    EXPECT_EQ(traffic.connection_id, "resource-connection");
    EXPECT_EQ(traffic.sent_bytes, 4096U);
    EXPECT_EQ(traffic.received_bytes, 512U);
    context->BeginStop();
    EXPECT_TRUE(context->StopAndWait(2s));
    context.reset();
    runtime->RequestDrain();
    runtime->Join();
}

TEST(WebRtcTransportLifecycle, QueuedEventsAreSafeWhenCallbackIsUnregisteredDuringStop) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    auto context = WebRtcExecutionContext::Create(runtime, "webrtc-unregister-test");
    ASSERT_TRUE(context);
    const auto delivered = std::make_shared<std::atomic_int>(0);
    context->SetEventCallback([delivered](const WebRtcEvent&) { delivered->fetch_add(1, std::memory_order_relaxed); });
    for (int event_index = 0; event_index < 100; ++event_index) {
        context->Publish(WebRtcInsertIdrEvent{});
    }
    context->SetEventCallback({});
    context->BeginStop();
    EXPECT_TRUE(context->StopAndWait(2s));
    EXPECT_LE(delivered->load(std::memory_order_relaxed), 100);
    context.reset();
    runtime->RequestDrain();
    runtime->Join();
}

}  // namespace
}  // namespace px
