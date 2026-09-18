#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>

#include "network/render_service_rpc_state.h"
#include "px_common/async_runtime.h"

namespace px {
namespace {

using namespace std::chrono_literals;

template <typename T>
PxAwaitable<void> AwaitServiceOperation(
    std::shared_ptr<PxAsyncOneShot<T>> operation,
    std::shared_ptr<std::promise<PxResult<T>>> completion) {
    completion->set_value(co_await PxAsyncOneShot<T>::WaitUntil(
        operation, std::chrono::steady_clock::now() + 2s));
    co_return;
}

TEST(RenderServiceRpcState, VirtualDisplayBusinessRejectionIsAValidResponse) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto state =
        std::make_shared<RenderServiceRpcState>(scope->Executor());
    auto registered =
        state->virtual_display_requests_->Register("display-request");
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

TEST(RenderServiceRpcState,
     FrontendAdmissionGrantPreservesBoundTargetAndLease) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto state =
        std::make_shared<RenderServiceRpcState>(scope->Executor());
    auto registered =
        state->frontend_admission_requests_->Register("admission-request");
    ASSERT_TRUE(registered.HasValue());
    const auto completion = std::make_shared<
        std::promise<PxResult<MsgFrontendAdmissionServiceResult>>>();
    auto future = completion->get_future();
    const auto operation = registered.Value();

    ASSERT_TRUE(scope->Spawn("frontend-admission", [operation, completion]() {
        return AwaitServiceOperation(operation, completion);
    }));
    MsgFrontendAdmissionServiceResult response;
    response.request_id_ = "admission-request";
    response.accepted_ = true;
    response.session_id_ = "session-1";
    response.revision_ = 8;
    response.target_kind_ = "cloud_application";
    response.application_id_ = "application-1";
    response.instance_id_ = "instance-1";
    response.client_type_ = "android";
    response.access_role_ = "controller";
    response.valid_for_ms_ = 29'000;
    ASSERT_TRUE(state->frontend_admission_requests_->Complete(
        response.request_id_,
        PxResult<MsgFrontendAdmissionServiceResult>::Success(response)));

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_TRUE(result.Value().accepted_);
    EXPECT_EQ(result.Value().target_kind_, "cloud_application");
    EXPECT_EQ(result.Value().instance_id_, "instance-1");
    EXPECT_EQ(result.Value().access_role_, "controller");
    EXPECT_EQ(result.Value().valid_for_ms_, 29'000U);
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderServiceRpcState,
     DisconnectFailsDisplayRequestAndLateResponseIsIgnored) {
    for (int round = 0; round < 10; ++round) {
        const auto runtime = PxAsyncRuntime::Create();
        ASSERT_TRUE(runtime->Start());
        const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
        const auto state =
            std::make_shared<RenderServiceRpcState>(scope->Executor());
        auto display = state->virtual_display_requests_->Register("display");
        ASSERT_TRUE(display.HasValue());

        const auto display_completion = std::make_shared<
            std::promise<PxResult<MsgVirtualDisplayServiceResult>>>();
        auto display_future = display_completion->get_future();
        const auto display_operation = display.Value();
        ASSERT_TRUE(scope->Spawn("display-disconnect", [display_operation,
                                                        display_completion]() {
            return AwaitServiceOperation(display_operation, display_completion);
        }));

        const auto disconnected =
            MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected,
                             "test_disconnect", "service disconnected", true);
        EXPECT_EQ(state->virtual_display_requests_->FailAll(disconnected), 1U);
        EXPECT_FALSE(state->virtual_display_requests_->Complete(
            "display", PxResult<MsgVirtualDisplayServiceResult>::Success({})));

        ASSERT_EQ(display_future.wait_for(2s), std::future_status::ready)
            << "round=" << round;
        EXPECT_EQ(display_future.get().Error().code,
                  PxAsyncErrorCode::kServiceNotConnected);
        ASSERT_TRUE(scope->WaitFor(2s));
        runtime->RequestStop();
        runtime->Join();
    }
}

TEST(RenderServiceRpcState,
     DisconnectFailsFrontendAdmissionAndLateGrantIsIgnored) {
    for (int round = 0; round < 10; ++round) {
        const auto runtime = PxAsyncRuntime::Create();
        ASSERT_TRUE(runtime->Start());
        const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
        const auto state =
            std::make_shared<RenderServiceRpcState>(scope->Executor());
        auto admission =
            state->frontend_admission_requests_->Register("admission");
        ASSERT_TRUE(admission.HasValue());

        const auto completion = std::make_shared<
            std::promise<PxResult<MsgFrontendAdmissionServiceResult>>>();
        auto future = completion->get_future();
        const auto operation = admission.Value();
        ASSERT_TRUE(
            scope->Spawn("admission-disconnect", [operation, completion]() {
                return AwaitServiceOperation(operation, completion);
            }));

        const auto disconnected =
            MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected,
                             "test_disconnect", "service disconnected", true);
        EXPECT_EQ(state->frontend_admission_requests_->FailAll(disconnected),
                  1U);
        EXPECT_FALSE(state->frontend_admission_requests_->Complete(
            "admission",
            PxResult<MsgFrontendAdmissionServiceResult>::Success({})));

        ASSERT_EQ(future.wait_for(2s), std::future_status::ready)
            << "round=" << round;
        EXPECT_EQ(future.get().Error().code,
                  PxAsyncErrorCode::kServiceNotConnected);
        ASSERT_TRUE(scope->WaitFor(2s));
        runtime->RequestStop();
        runtime->Join();
    }
}

}  // namespace
}  // namespace px
