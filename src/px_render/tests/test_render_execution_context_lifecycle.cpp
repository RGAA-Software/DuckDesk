#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "architecture/events/render_event.h"
#include "architecture/modules/render_module.h"
#include "architecture/runtime/render_execution_context.h"
#include "px_common_new/async_runtime.h"

namespace px {
namespace {

using namespace std::chrono_literals;

class TestRenderModule final : public RenderModule {
  public:
    [[nodiscard]] std::string Id() const override {
        return "test_render_module";
    }
    [[nodiscard]] RenderModuleKind Kind() const override {
        return RenderModuleKind::kSource;
    }
};

class RenderExecutionContextLifecycleTest : public testing::Test {
  protected:
    void SetUp() override {
        runtime_ = PxAsyncRuntime::Create({.worker_threads = 1});
        ASSERT_TRUE(runtime_);
        ASSERT_TRUE(runtime_->Start());
    }

    void TearDown() override {
        runtime_->RequestDrain();
        runtime_->Join();
    }

    std::shared_ptr<PxAsyncRuntime> runtime_;
};

TEST_F(RenderExecutionContextLifecycleTest, StopFromOwnedCallbackDoesNotSelfJoin) {
    auto context = RenderExecutionContext::Create(runtime_, "self-stop");
    ASSERT_TRUE(context);
    const std::weak_ptr<RenderExecutionContext> weak_context = context;
    const auto completion = std::make_shared<std::promise<void>>();
    auto future = completion->get_future();

    ASSERT_TRUE(context->Post([weak_context, completion] {
        if (const auto owner = weak_context.lock()) {
            owner->Stop();
        }
        completion->set_value();
    }));
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    context.reset();
}

TEST_F(RenderExecutionContextLifecycleTest, StopCancelsDelayedCallback) {
    auto context = RenderExecutionContext::Create(runtime_, "cancel-delay");
    ASSERT_TRUE(context);
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    ASSERT_TRUE(context->PostDelayed(250ms, [callback_count] { ++*callback_count; }));
    context->Stop();
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(callback_count->load(), 0);
}

TEST_F(RenderExecutionContextLifecycleTest, ModuleUnregisterAndDestroySilenceSavedDispatcher) {
    const auto module = std::make_shared<TestRenderModule>();
    ASSERT_TRUE(module->Start(RenderModuleConfiguration{.async_runtime = runtime_, .instance_name = "test-module"}));
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    module->SetEventCallback([callback_count](const RenderEventEnvelope&) { ++*callback_count; });
    const auto dispatcher = module->MakeImmediateEventDispatcher();

    module->SetEventCallback({});
    dispatcher(RenderEventEnvelope{.source_id = module->Id(), .payload = std::make_shared<DataSentEvent>()});
    EXPECT_EQ(callback_count->load(), 0);

    ASSERT_TRUE(module->Destroy());
    dispatcher(RenderEventEnvelope{.source_id = module->Id(), .payload = std::make_shared<DataSentEvent>()});
    EXPECT_EQ(callback_count->load(), 0);
}

TEST_F(RenderExecutionContextLifecycleTest, ModuleCanShutdownFromEventCallback) {
    const auto module = std::make_shared<TestRenderModule>();
    ASSERT_TRUE(module->Start(RenderModuleConfiguration{.async_runtime = runtime_, .instance_name = "callback-stop"}));
    const std::weak_ptr<TestRenderModule> weak_module = module;
    module->SetEventCallback([weak_module](const RenderEventEnvelope&) {
        if (const auto owner = weak_module.lock()) {
            static_cast<void>(owner->Destroy());
        }
    });
    module->EmitEventImmediately(std::make_shared<DataSentEvent>());
    EXPECT_TRUE(module->IsStoppingOrDestroyed());
    EXPECT_TRUE(module->Destroy());
}

TEST_F(RenderExecutionContextLifecycleTest, RepeatedCreatePostAndStopIsSafe) {
    for (int round = 0; round < 100; ++round) {
        auto context = RenderExecutionContext::Create(runtime_, "repeat-" + std::to_string(round));
        ASSERT_TRUE(context);
        const auto completion = std::make_shared<std::promise<void>>();
        auto future = completion->get_future();
        ASSERT_TRUE(context->Post([completion] { completion->set_value(); }));
        ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
        context->Stop();
        context->Stop();
    }
}

} // namespace
} // namespace px
