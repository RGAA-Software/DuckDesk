#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>

#include "network/render_service_rpc_state.h"
#include "px_common_new/async_runtime.h"

namespace px {
namespace {

using namespace std::chrono_literals;

template<typename T>
PxAwaitable<void> AwaitServiceOperation(
    std::shared_ptr<PxAsyncOneShot<T>> operation,
    std::shared_ptr<std::promise<PxResult<T>>> completion) {
    completion->set_value(co_await PxAsyncOneShot<T>::WaitUntil(operation,
        std::chrono::steady_clock::now() + 2s));
    co_return;
}

TEST(RenderServiceRpcState, TicketResponsePreservesPermissionsAndIceConfig) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto state = std::make_shared<RenderServiceRpcState>(scope->Executor());
    auto registered = state->ticket_requests_->Register("ticket-request");
    ASSERT_TRUE(registered.HasValue());
    const auto completion =
        std::make_shared<std::promise<PxResult<RedeemedConnectionTicket>>>();
    auto future = completion->get_future();
    const auto operation = registered.Value();

    ASSERT_TRUE(scope->Spawn("ticket", [operation, completion]() {
        return AwaitServiceOperation(operation, completion);
    }));
    RedeemedConnectionTicket ticket{
        .permissions = {"view", "file"},
        .rtc_ice_config_json = "{\"iceServers\":[]}",
    };
    ASSERT_TRUE(state->ticket_requests_->Complete(
        "ticket-request",
        PxResult<RedeemedConnectionTicket>::Success(std::move(ticket))));

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().permissions,
              (std::vector<std::string>{"view", "file"}));
    EXPECT_EQ(result.Value().rtc_ice_config_json, "{\"iceServers\":[]}");
    EXPECT_EQ(state->ticket_requests_->Size(), 0U);
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderServiceRpcState, VirtualDisplayBusinessRejectionIsAValidResponse) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto state = std::make_shared<RenderServiceRpcState>(scope->Executor());
    auto registered = state->virtual_display_requests_->Register("display-request");
    ASSERT_TRUE(registered.HasValue());
    const auto completion = std::make_shared<
        std::promise<PxResult<MsgVirtualDisplayServiceResult>>>();
    auto future = completion->get_future();
    const auto operation = registered.Value();

    ASSERT_TRUE(scope->Spawn("virtual-display", [operation, completion]() {
        return AwaitServiceOperation(operation, completion);
    }));
    MsgVirtualDisplayServiceResult response;
    response.request_id_ = "display-request";
    response.accepted_ = false;
    response.error_code_ = "DRIVER_BUSY";
    response.error_message_ = "driver is applying another topology";
    ASSERT_TRUE(state->virtual_display_requests_->Complete(
        response.request_id_,
        PxResult<MsgVirtualDisplayServiceResult>::Success(response)));

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_FALSE(result.Value().accepted_);
    EXPECT_EQ(result.Value().error_code_, "DRIVER_BUSY");
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderServiceRpcState, DisconnectFailsBothKindsAndLateResponsesAreIgnored) {
    for (int round = 0; round < 10; ++round) {
        const auto runtime = PxAsyncRuntime::Create();
        ASSERT_TRUE(runtime->Start());
        const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
        const auto state = std::make_shared<RenderServiceRpcState>(scope->Executor());
        auto ticket = state->ticket_requests_->Register("ticket");
        auto display = state->virtual_display_requests_->Register("display");
        ASSERT_TRUE(ticket.HasValue());
        ASSERT_TRUE(display.HasValue());

        const auto ticket_completion =
            std::make_shared<std::promise<PxResult<RedeemedConnectionTicket>>>();
        const auto display_completion = std::make_shared<
            std::promise<PxResult<MsgVirtualDisplayServiceResult>>>();
        auto ticket_future = ticket_completion->get_future();
        auto display_future = display_completion->get_future();
        const auto ticket_operation = ticket.Value();
        const auto display_operation = display.Value();
        ASSERT_TRUE(scope->Spawn("ticket-disconnect",
            [ticket_operation, ticket_completion]() {
                return AwaitServiceOperation(ticket_operation, ticket_completion);
            }));
        ASSERT_TRUE(scope->Spawn("display-disconnect",
            [display_operation, display_completion]() {
                return AwaitServiceOperation(display_operation, display_completion);
            }));

        const auto disconnected = MakePxAsyncError(
            PxAsyncErrorCode::kServiceNotConnected,
            "test_disconnect",
            "service disconnected",
            true);
        EXPECT_EQ(state->ticket_requests_->FailAll(disconnected), 1U);
        EXPECT_EQ(state->virtual_display_requests_->FailAll(disconnected), 1U);
        EXPECT_FALSE(state->ticket_requests_->Complete(
            "ticket",
            PxResult<RedeemedConnectionTicket>::Success({})));
        EXPECT_FALSE(state->virtual_display_requests_->Complete(
            "display",
            PxResult<MsgVirtualDisplayServiceResult>::Success({})));

        ASSERT_EQ(ticket_future.wait_for(2s), std::future_status::ready)
            << "round=" << round;
        ASSERT_EQ(display_future.wait_for(2s), std::future_status::ready)
            << "round=" << round;
        EXPECT_EQ(ticket_future.get().Error().code,
                  PxAsyncErrorCode::kServiceNotConnected);
        EXPECT_EQ(display_future.get().Error().code,
                  PxAsyncErrorCode::kServiceNotConnected);
        ASSERT_TRUE(scope->WaitFor(2s));
        runtime->RequestStop();
        runtime->Join();
    }
}

} // namespace
} // namespace px
