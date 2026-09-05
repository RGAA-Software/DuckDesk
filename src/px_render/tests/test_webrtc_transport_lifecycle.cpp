#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "px_render/network/webrtc_transport_host.h"
#include "px_render/network/webrtc/webrtc_execution_context.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxAwaitable<void> CollectWebRtcStop(std::shared_ptr<WebRtcTransportHandle> transport, std::shared_ptr<std::promise<PxResult<void>>> completion) {
    completion->set_value(co_await WebRtcTransportHandle::StopAsync(transport, std::chrono::steady_clock::now() + 5s));
}

void RunLifecycleRounds(const std::filesystem::path& rtc_path, const std::filesystem::path& rtc_local_path) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    ASSERT_TRUE(scope);
    for (int round = 0; round < 100; ++round) {
        auto host = WebRtcTransportHost::Create();
        auto transports = host->CreateTransports();
        ASSERT_EQ(transports.size(), 2U) << "round " << round;

        const WebRtcTransportConfiguration configuration{
            .async_runtime = runtime,
            .base_path = std::filesystem::path(PX_WEBRTC_REMOTE_LIBRARY_PATH).parent_path().generic_string(),
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
        EXPECT_NE(GetModuleHandleW(rtc_path.filename().c_str()), nullptr);
        EXPECT_NE(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);
    }
    scope->BeginStop();
    EXPECT_TRUE(scope->WaitFor(1s));
    runtime->RequestDrain();
    runtime->Join();
}

TEST(WebRtcTransportLifecycle, RapidStartStopWithDirectlyLinkedDlls) {
    RunLifecycleRounds(PX_WEBRTC_REMOTE_LIBRARY_PATH, PX_WEBRTC_LOCAL_LIBRARY_PATH);
}

TEST(WebRtcTransportLifecycle, DllsRemainLoadedForProcessLifetime) {
    const auto rtc_path = std::filesystem::path(PX_WEBRTC_REMOTE_LIBRARY_PATH);
    const auto rtc_local_path = std::filesystem::path(PX_WEBRTC_LOCAL_LIBRARY_PATH);
    auto host = WebRtcTransportHost::Create();
    auto transports = host->CreateTransports();
    ASSERT_EQ(transports.size(), 2U);
    EXPECT_EQ(transports[0]->Kind(), WebRtcTransportKind::kRemote);
    EXPECT_EQ(transports[0]->BaseName(), "px_render_rtc_remote");
    EXPECT_EQ(transports[1]->Kind(), WebRtcTransportKind::kLocal);
    EXPECT_EQ(transports[1]->BaseName(), "px_render_rtc");

    host->Reset();
    host.reset();

    // Directly linked DLLs remain process dependencies after transport owners are released.
    EXPECT_NE(GetModuleHandleW(rtc_path.filename().c_str()), nullptr);
    EXPECT_NE(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);

    transports.clear();
    EXPECT_NE(GetModuleHandleW(rtc_path.filename().c_str()), nullptr);
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

} // namespace
} // namespace px
