#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <format>
#include <future>
#include <latch>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "diagnostics/performance_window.h"
#include "diagnostics/rate_limited_log.h"
#include "diagnostics/render_error.h"
#include "diagnostics/render_log_context.h"
#include "diagnostics/transport_performance_window.h"
#include "extensions/flow_node_plugin_registry.h"
#include "pipeline/bounded_media_queue.h"
#include "pipeline/media_types.h"
#include "modules/builtin_module_catalog.h"
#include "observers/frame_debugger_observer.h"
#include "observers/pipeline_statistics_observer.h"
#include "pipeline/encoded_media_bus.h"
#include "runtime/scoped_subscription.h"
#include "runtime/await_callback.h"
#include "runtime/render_composition_root.h"
#include "px_common/privacy_log.h"

namespace px::render {
namespace {

using namespace std::chrono_literals;

class TestObserverFlowNode final : public ObserverPlugin {
  public:
    explicit TestObserverFlowNode(FlowNodeDescriptor descriptor) : descriptor_(std::move(descriptor)) {}

    [[nodiscard]] const FlowNodeDescriptor& Descriptor() const noexcept override {
        return descriptor_;
    }

    [[nodiscard]] bool IsEnabled() const noexcept override {
        return enabled_;
    }

    [[nodiscard]] FlowNodeLifecycleResult SetEnabled(const bool enabled) override {
        enabled_ = enabled;
        return {};
    }

    [[nodiscard]] PxAwaitable<FlowNodeLifecycleResult> Start(FlowNodeStartContext context) override {
        active_scope_ = std::move(context.async_scope);
        co_return FlowNodeLifecycleResult{};
    }

    [[nodiscard]] PxAwaitable<FlowNodeLifecycleResult> Stop() override {
        active_scope_.reset();
        co_return FlowNodeLifecycleResult{};
    }

    void ObserveCapturedVideo(const std::shared_ptr<const CapturedVideoFrame>&) noexcept override {}
    void ObserveEncodedVideo(const std::shared_ptr<const EncodedVideoFrame>&) noexcept override {}
    void ObserveCapturedAudio(const std::shared_ptr<const CapturedAudioFrame>&) noexcept override {}
    void ObserveEncodedAudio(const std::shared_ptr<const EncodedAudioFrame>&) noexcept override {}

  private:
    FlowNodeDescriptor descriptor_{};
    std::shared_ptr<PxAsyncScope> active_scope_{};
    bool enabled_{true};
};

FlowNodeDescriptor MakeTestObserverDescriptor(std::string id) {
    return FlowNodeDescriptor{
        .id = std::move(id),
        .name = "Test Observer",
        .author = "GammaRay",
        .description = "Flow-node registry test observer",
        .version_name = "1.0.0",
        .version_code = 1,
        .role = FlowNodeRole::kObserver,
        .default_enabled = true,
    };
}

std::shared_ptr<const std::vector<std::uint8_t>> MakePayload(const std::size_t size) {
    return std::make_shared<const std::vector<std::uint8_t>>(size);
}

template <typename T, typename Starter>
PxAwaitable<void> CompleteAwaitedCallback(Starter starter, const std::chrono::steady_clock::time_point deadline,
                                          std::shared_ptr<std::promise<PxResult<T>>> completion) {
    completion->set_value(co_await AwaitOwnedCallback<T>(std::move(starter), deadline, "architecture_test"));
    co_return;
}

RenderError MakeModuleTestError(const RenderErrorCode code, std::string operation, std::string reason) {
    return RenderError{
        .code = code,
        .component = "test_module",
        .operation = std::move(operation),
        .stage = "test_lifecycle",
        .reason = std::move(reason),
        .recoverable = false,
    };
}

BuiltinModuleRegistration MakeTestModule(std::string id, std::vector<std::string> dependencies,
                                         const std::shared_ptr<std::vector<std::string>>& events, ModuleLifecycleResult start_result = {},
                                         ModuleLifecycleResult stop_result = {}) {
    const auto module_id = id;
    return BuiltinModuleRegistration{
        .descriptor =
            BuiltinModuleDescriptor{
                .id = std::move(id),
                .name = "Test Module " + module_id,
                .author = "GammaRay",
                .description = "Render architecture lifecycle test module",
                .version_name = "1.0.0",
                .version_code = 1,
                .capability = BuiltinModuleCapability::kObserver,
                .default_enabled = true,
                .dependencies = std::move(dependencies),
            },
        .start = [events, module_id, start_result]() -> PxAwaitable<ModuleLifecycleResult> {
            events->push_back("start:" + module_id);
            co_return start_result;
        },
        .stop = [events, module_id, stop_result]() -> PxAwaitable<ModuleLifecycleResult> {
            events->push_back("stop:" + module_id);
            co_return stop_result;
        },
        .set_enabled =
            [events, module_id](const bool enabled) {
                events->push_back(std::string(enabled ? "enable:" : "disable:") + module_id);
                return ModuleLifecycleResult{};
            },
    };
}

PxAwaitable<void> CompleteFrameDebuggerStop(std::shared_ptr<FrameDebuggerObserver> observer, const std::chrono::steady_clock::time_point deadline,
                                            std::shared_ptr<std::promise<ModuleLifecycleResult>> completion) {
    completion->set_value(co_await FrameDebuggerObserver::StopAsync(observer, deadline));
    co_return;
}

TEST(RenderArchitectureFlowNodeRegistry, CreatesTypedNodeThroughCompositionRoot) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto registry = FlowNodePluginRegistry::Create();
    auto root = RenderCompositionRoot::Create(runtime, BuiltinModuleCatalog::Create(), registry);
    ASSERT_TRUE(root);

    const auto descriptor = MakeTestObserverDescriptor("test.observer");
    ASSERT_TRUE(root->RegisterFlowNodePlugin(FlowNodePluginRegistration{
        .descriptor = descriptor,
        .create = [descriptor]() { return std::make_shared<TestObserverFlowNode>(descriptor); },
        .dependencies = {},
    }));

    const auto created = root->CreateFlowNodePlugin(descriptor.id);
    ASSERT_TRUE(created);
    EXPECT_TRUE(std::dynamic_pointer_cast<ObserverPlugin>(*created));
    EXPECT_EQ((*created)->Descriptor().id, descriptor.id);
    const auto descriptors = root->SnapshotFlowNodePlugins();
    ASSERT_EQ(descriptors.size(), 1U);
    EXPECT_EQ(descriptors.front().role, FlowNodeRole::kObserver);

    root.reset();
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderArchitectureFlowNodeRegistry, RejectsDuplicateAndRoleMismatch) {
    const auto registry = FlowNodePluginRegistry::Create();
    const auto descriptor = MakeTestObserverDescriptor("test.duplicate");
    const auto registration = FlowNodePluginRegistration{
        .descriptor = descriptor,
        .create = [descriptor]() { return std::make_shared<TestObserverFlowNode>(descriptor); },
        .dependencies = {},
    };
    ASSERT_TRUE(registry->Register(registration));
    const auto duplicate = registry->Register(registration);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, RenderErrorCode::kModuleAlreadyRegistered);

    auto mismatched_descriptor = MakeTestObserverDescriptor("test.mismatch");
    mismatched_descriptor.role = FlowNodeRole::kSink;
    auto observer_descriptor = mismatched_descriptor;
    observer_descriptor.role = FlowNodeRole::kObserver;
    ASSERT_TRUE(registry->Register(FlowNodePluginRegistration{
        .descriptor = mismatched_descriptor,
        .create = [observer_descriptor]() { return std::make_shared<TestObserverFlowNode>(observer_descriptor); },
        .dependencies = {},
    }));
    const auto mismatched = registry->CreateNode(mismatched_descriptor.id);
    ASSERT_FALSE(mismatched);
    EXPECT_EQ(mismatched.error().code, RenderErrorCode::kModuleInvalidDescriptor);
}

TEST(RenderArchitectureMediaTypes, ValidFrameOwnsImmutablePayload) {
    auto created = CapturedVideoFrame::Create(
        FrameIdentity{
            .stream_id = "stream-a",
            .monitor_id = "monitor-a",
            .frame_index = 7,
            .timestamp_us = 1234,
            .topology_generation = 3,
        },
        1920, 1080, VideoPixelFormat::kBgra8, MakePayload(64));

    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created->Identity().stream_id, "stream-a");
    EXPECT_EQ(created->Identity().topology_generation, 3U);
    EXPECT_EQ(created->Width(), 1920U);
    EXPECT_EQ(created->Height(), 1080U);
    EXPECT_EQ(created->Payload()->size(), 64U);
}

TEST(RenderArchitectureStatisticsObserver, TypedSubscriptionsStopAccountingAfterUnregister) {
    const auto bus = EncodedMediaBus::Create();
    auto observer = PipelineStatisticsObserver::Create(bus);
    ASSERT_TRUE(observer);
    ASSERT_TRUE(observer->Start());
    EXPECT_TRUE(bus->NeedsVideo());
    EXPECT_TRUE(bus->NeedsCapturedVideo());
    EXPECT_TRUE(bus->NeedsEncodedAudio());
    EXPECT_TRUE(bus->NeedsCapturedAudio());

    bus->PublishVideo(std::make_shared<const EncodedVideoFrame>(EncodedVideoFrame{
        .identity = FrameIdentity{.monitor_id = "monitor-a"},
        .codec = "h264",
        .width = 640,
        .height = 480,
        .key_frame = true,
        .payload = MakePayload(64),
    }));
    auto captured_video = CapturedVideoFrame::Create(
        FrameIdentity{
            .stream_id = "stream-a",
            .monitor_id = "monitor-a",
        },
        4, 4, VideoPixelFormat::kBgra8, MakePayload(64));
    ASSERT_TRUE(captured_video);
    bus->PublishCapturedVideo(std::make_shared<const CapturedVideoFrame>(std::move(*captured_video)));
    bus->PublishEncodedAudio(std::make_shared<const EncodedAudioFrame>(EncodedAudioFrame{
        .codec = "opus",
        .payload = MakePayload(16),
    }));
    bus->PublishCapturedAudio(std::make_shared<const CapturedAudioFrame>(CapturedAudioFrame{
        .sample_rate_hz = 48000,
        .channels = 2,
        .bits_per_sample = 16,
        .payload = MakePayload(128),
    }));
    bus->PublishClientConnected(MediaClientConnected{
        .visitor_device_id = "client-a",
        .stream_id = "stream-a",
        .transport = "RTC",
    });
    const auto active = observer->Snapshot();
    EXPECT_EQ(active.encoded_video_frames, 1U);
    EXPECT_EQ(active.encoded_video_bytes, 64U);
    EXPECT_EQ(active.captured_video_frames, 1U);
    EXPECT_EQ(active.captured_video_bytes, 64U);
    EXPECT_EQ(active.encoded_audio_packets, 1U);
    EXPECT_EQ(active.encoded_audio_bytes, 16U);
    EXPECT_EQ(active.captured_audio_frames, 1U);
    EXPECT_EQ(active.captured_audio_bytes, 128U);
    EXPECT_EQ(active.connected_clients, 1U);

    ASSERT_TRUE(observer->Stop());
    EXPECT_FALSE(bus->NeedsVideo());
    EXPECT_FALSE(bus->NeedsCapturedVideo());
    EXPECT_FALSE(bus->NeedsEncodedAudio());
    EXPECT_FALSE(bus->NeedsCapturedAudio());
    bus->PublishVideo(std::make_shared<const EncodedVideoFrame>(EncodedVideoFrame{.payload = MakePayload(32)}));
    EXPECT_EQ(observer->Snapshot().encoded_video_frames, 1U);
}

TEST(RenderArchitectureMediaTypes, InvalidFrameReturnsStableTypedError) {
    auto created = CapturedVideoFrame::Create(FrameIdentity{}, 0, 1080, VideoPixelFormat::kNv12, MakePayload(4));

    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code, RenderErrorCode::kPipelineInvalidFrame);
    EXPECT_EQ(StableErrorCode(created.error().code), "PIPELINE_INVALID_FRAME");
    EXPECT_TRUE(created.error().recoverable);
    EXPECT_TRUE(created.error().IsValid());
}

TEST(RenderArchitectureQueue, DropOldestRetainsNewestFrames) {
    BoundedMediaQueue<int> queue(2, QueueOverflowPolicy::kDropOldest);
    EXPECT_EQ(queue.Submit(std::make_shared<const int>(1)), QueueSubmitResult::kAccepted);
    EXPECT_EQ(queue.Submit(std::make_shared<const int>(2)), QueueSubmitResult::kAccepted);
    EXPECT_EQ(queue.Submit(std::make_shared<const int>(3)), QueueSubmitResult::kAcceptedAfterDroppingOldest);

    const auto first = queue.TryPop();
    const auto second = queue.TryPop();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(**first, 2);
    EXPECT_EQ(**second, 3);
    const auto snapshot = queue.Snapshot();
    EXPECT_EQ(snapshot.accepted, 3U);
    EXPECT_EQ(snapshot.dropped, 1U);
    EXPECT_EQ(snapshot.high_watermark, 2U);
}

TEST(RenderArchitectureQueue, DropNewestPreservesExistingOrder) {
    BoundedMediaQueue<int> queue(2, QueueOverflowPolicy::kDropNewest);
    EXPECT_EQ(queue.Submit(std::make_shared<const int>(1)), QueueSubmitResult::kAccepted);
    EXPECT_EQ(queue.Submit(std::make_shared<const int>(2)), QueueSubmitResult::kAccepted);
    EXPECT_EQ(queue.Submit(std::make_shared<const int>(3)), QueueSubmitResult::kDroppedNewest);

    ASSERT_TRUE(queue.TryPop().has_value());
    const auto second = queue.TryPop();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(**second, 2);
}

TEST(RenderArchitectureQueue, CloseModesHaveExplicitLifetimeSemantics) {
    BoundedMediaQueue<int> draining(1, QueueOverflowPolicy::kDropOldest);
    ASSERT_EQ(draining.Submit(std::make_shared<const int>(8)), QueueSubmitResult::kAccepted);
    draining.Close(QueueCloseMode::kDrain);
    EXPECT_TRUE(draining.TryPop().has_value());
    EXPECT_EQ(draining.Submit(std::make_shared<const int>(9)), QueueSubmitResult::kRejectedClosed);

    BoundedMediaQueue<int> cancelling(1, QueueOverflowPolicy::kDropOldest);
    ASSERT_EQ(cancelling.Submit(std::make_shared<const int>(8)), QueueSubmitResult::kAccepted);
    cancelling.Close(QueueCloseMode::kCancel);
    EXPECT_FALSE(cancelling.TryPop().has_value());
    EXPECT_TRUE(cancelling.Snapshot().closed);
}

TEST(RenderArchitectureDiagnostics, PerformanceWindowIsBoundedAndResettable) {
    PerformanceWindow window(3);
    window.Observe(10);
    window.Observe(20);
    window.Observe(30);
    window.Observe(40);

    auto snapshot = window.Snapshot();
    EXPECT_EQ(snapshot.total_observations, 4U);
    EXPECT_EQ(snapshot.sample_count, 3U);
    EXPECT_EQ(snapshot.evicted_samples, 1U);
    EXPECT_EQ(snapshot.minimum_us, 20);
    EXPECT_EQ(snapshot.maximum_us, 40);
    EXPECT_EQ(snapshot.average_us, 30);
    EXPECT_EQ(snapshot.p50_us, 30);
    EXPECT_EQ(snapshot.p95_us, 40);
    EXPECT_EQ(snapshot.p99_us, 40);

    window.Reset();
    snapshot = window.Snapshot();
    EXPECT_EQ(snapshot.total_observations, 0U);
    EXPECT_EQ(snapshot.sample_count, 0U);
}

TEST(RenderArchitectureDiagnostics, TransportWindowAggregatesAndResets) {
    const auto start = std::chrono::steady_clock::time_point{};
    TransportPerformanceWindow window(5s, start);
    window.ObserveInbound(100);
    window.ObserveInbound(50);
    window.ObserveOutbound(75);
    window.ObserveDropped();
    window.ObserveConnected();
    window.ObserveDisconnected();
    window.ObserveQueueDepth(3);
    window.ObserveQueueDepth(9);

    EXPECT_FALSE(window.SnapshotAndReset(start + 4s, 2, 4));
    const auto snapshot = window.SnapshotAndReset(start + 5s, 2, 4);
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->window_ms, 5000U);
    EXPECT_EQ(snapshot->inbound_messages, 2U);
    EXPECT_EQ(snapshot->inbound_bytes, 150U);
    EXPECT_EQ(snapshot->outbound_messages, 1U);
    EXPECT_EQ(snapshot->outbound_bytes, 75U);
    EXPECT_EQ(snapshot->dropped_messages, 1U);
    EXPECT_EQ(snapshot->connected, 1U);
    EXPECT_EQ(snapshot->disconnected, 1U);
    EXPECT_EQ(snapshot->active_connections, 2U);
    EXPECT_EQ(snapshot->queue_depth, 4U);
    EXPECT_EQ(snapshot->queue_high_watermark, 9U);

    const auto empty = window.SnapshotAndReset(start + 10s, 0, 0);
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->inbound_messages, 0U);
    EXPECT_EQ(empty->outbound_messages, 0U);
    EXPECT_EQ(empty->queue_high_watermark, 4U);
}

TEST(RenderArchitectureDiagnostics, TransportWindowAccountingIsConcurrent) {
    const auto start = std::chrono::steady_clock::time_point{};
    const auto window = std::make_shared<TransportPerformanceWindow>(1s, start);
    std::vector<std::jthread> writers;
    for (int worker = 0; worker < 4; ++worker) {
        writers.emplace_back([window]() {
            for (int sample = 0; sample < 1000; ++sample) {
                window->ObserveInbound(2);
                window->ObserveOutbound(3);
            }
        });
    }
    writers.clear();

    const auto snapshot = window->SnapshotAndReset(start + 1s, 1, 0);
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->inbound_messages, 4000U);
    EXPECT_EQ(snapshot->inbound_bytes, 8000U);
    EXPECT_EQ(snapshot->outbound_messages, 4000U);
    EXPECT_EQ(snapshot->outbound_bytes, 12000U);
}

TEST(RenderArchitectureDiagnostics, LogContextIsOwnedValueState) {
    RenderLogContext context{
        .trace_token = "trace-1",
        .stream_token = "stream-1",
        .transport_kind = "webrtc",
    };
    const auto copied = context;
    context.trace_token = "trace-2";

    EXPECT_TRUE(copied.HasCorrelation());
    EXPECT_EQ(copied.trace_token, "trace-1");
    EXPECT_EQ(copied.transport_kind, "webrtc");
}

TEST(RenderArchitectureDiagnostics, PrivacyLogIdIsStableAndDoesNotExposeInput) {
    constexpr std::string_view sensitive = "render_privacy_canary_ticket_7f0c90f2";
    const auto first = PrivacyLogId(sensitive);
    const auto second = PrivacyLogId(sensitive);
    const auto different = PrivacyLogId("render_privacy_canary_ticket_other");

    EXPECT_EQ(first, second);
    EXPECT_NE(first, different);
    EXPECT_EQ(first.size(), 8U);
    EXPECT_EQ(first.find(sensitive), std::string::npos);
}

TEST(RenderArchitectureDiagnostics, RateLimitReportsSuppressedOccurrences) {
    RateLimitedLogGate gate(1s, 4);
    const auto start = std::chrono::steady_clock::time_point{};

    auto decision = gate.Evaluate("OBSERVER_QUEUE_OVERFLOW", start);
    EXPECT_TRUE(decision.emit);
    EXPECT_EQ(decision.suppressed_since_last_emit, 0U);
    EXPECT_FALSE(gate.Evaluate("OBSERVER_QUEUE_OVERFLOW", start + 100ms).emit);
    EXPECT_FALSE(gate.Evaluate("OBSERVER_QUEUE_OVERFLOW", start + 900ms).emit);

    decision = gate.Evaluate("OBSERVER_QUEUE_OVERFLOW", start + 1s);
    EXPECT_TRUE(decision.emit);
    EXPECT_EQ(decision.suppressed_since_last_emit, 2U);
}

TEST(RenderArchitectureDiagnostics, RateLimitKeyStorageIsBounded) {
    RateLimitedLogGate gate(1s, 2);
    const auto start = std::chrono::steady_clock::time_point{};
    EXPECT_TRUE(gate.Evaluate("a", start).emit);
    EXPECT_TRUE(gate.Evaluate("b", start).emit);
    EXPECT_TRUE(gate.Evaluate("c", start).emit);
    EXPECT_EQ(gate.TrackedKeyCount(), 2U);
}

TEST(RenderArchitectureSubscription, TokenDestructionUnregistersCallback) {
    const auto registry = SubscriptionRegistry<int>::Create();
    const auto calls = std::make_shared<int>(0);
    const auto callback = std::make_shared<SubscriptionRegistry<int>::Callback>([calls](const int& value) { *calls += value; });

    auto subscription = registry->Subscribe(callback);
    registry->Dispatch(2);
    EXPECT_EQ(*calls, 2);
    subscription.reset();
    registry->Dispatch(3);
    EXPECT_EQ(*calls, 2);
    EXPECT_EQ(registry->Size(), 0U);
}

TEST(RenderArchitectureSubscription, RegistryDoesNotOwnObserverCallback) {
    const auto registry = SubscriptionRegistry<int>::Create();
    const auto calls = std::make_shared<int>(0);
    auto callback = std::make_shared<SubscriptionRegistry<int>::Callback>([calls](const int&) { ++*calls; });
    const auto subscription = registry->Subscribe(callback);

    callback.reset();
    registry->Dispatch(1);
    EXPECT_EQ(*calls, 0);
    EXPECT_EQ(registry->Size(), 0U);
}

TEST(RenderArchitectureSubscription, CallbackCanUnregisterLaterCallback) {
    const auto registry = SubscriptionRegistry<int>::Create();
    const auto calls = std::make_shared<std::vector<std::string>>();
    const auto second_subscription = std::make_shared<std::shared_ptr<ScopedSubscription>>();
    const auto first_callback = std::make_shared<SubscriptionRegistry<int>::Callback>([calls, second_subscription](const int&) {
        calls->push_back("first");
        if (*second_subscription) {
            (*second_subscription)->Reset();
        }
    });
    const auto second_callback = std::make_shared<SubscriptionRegistry<int>::Callback>([calls](const int&) { calls->push_back("second"); });

    const auto first_token = registry->Subscribe(first_callback);
    *second_subscription = registry->Subscribe(second_callback);
    registry->Dispatch(1);

    EXPECT_EQ(*calls, (std::vector<std::string>{"first"}));
    EXPECT_EQ(registry->Size(), 1U);
    EXPECT_TRUE(first_token->IsActive());
}

TEST(RenderArchitectureSubscription, ConcurrentResetPreventsQueuedInvocation) {
    const auto registry = SubscriptionRegistry<int>::Create();
    const auto callback_entered = std::make_shared<std::latch>(1);
    const auto allow_callback_exit = std::make_shared<std::latch>(1);
    const auto first_callback = std::make_shared<SubscriptionRegistry<int>::Callback>([callback_entered, allow_callback_exit](const int&) {
        callback_entered->count_down();
        allow_callback_exit->wait();
    });
    const auto later_calls = std::make_shared<int>(0);
    const auto later_callback = std::make_shared<SubscriptionRegistry<int>::Callback>([later_calls](const int&) { ++*later_calls; });
    const auto first_token = registry->Subscribe(first_callback);
    auto later_token = registry->Subscribe(later_callback);

    std::jthread dispatch_thread([registry]() { registry->Dispatch(1); });
    callback_entered->wait();
    later_token->Reset();
    allow_callback_exit->count_down();
    dispatch_thread.join();

    EXPECT_EQ(*later_calls, 0);
    EXPECT_TRUE(first_token->IsActive());
}

TEST(RenderArchitectureAwaitCallback, SynchronousCompletionIsAwaitable) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto completion = std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();
    const auto starter = [](OwnedCallbackCompletion<int> callback) {
        callback(PxResult<int>::Success(42));
        return true;
    };

    ASSERT_TRUE(scope->Spawn("synchronous_callback", [starter, completion]() {
        return CompleteAwaitedCallback<int>(starter, std::chrono::steady_clock::now() + 1s, completion);
    }));
    ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 42);
    EXPECT_TRUE(scope->StopAndWait(1s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderArchitectureAwaitCallback, DuplicateCompletionUsesFirstResult) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto completion = std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();
    const auto starter = [](OwnedCallbackCompletion<int> callback) {
        callback(PxResult<int>::Success(1));
        callback(PxResult<int>::Success(2));
        return true;
    };

    ASSERT_TRUE(scope->Spawn("duplicate_callback", [starter, completion]() {
        return CompleteAwaitedCallback<int>(starter, std::chrono::steady_clock::now() + 1s, completion);
    }));
    ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 1);
    EXPECT_TRUE(scope->StopAndWait(1s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderArchitectureAwaitCallback, TimeoutMakesLateCallbackHarmless) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto completion = std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();
    const auto callback_ready = std::make_shared<std::latch>(1);
    const auto callback_holder = std::make_shared<OwnedCallbackCompletion<int>>();
    const auto starter = [callback_ready, callback_holder](OwnedCallbackCompletion<int> callback) {
        *callback_holder = std::move(callback);
        callback_ready->count_down();
        return true;
    };

    ASSERT_TRUE(scope->Spawn("timeout_callback", [starter, completion]() {
        return CompleteAwaitedCallback<int>(starter, std::chrono::steady_clock::now() + 20ms, completion);
    }));
    callback_ready->wait();
    ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    (*callback_holder)(PxResult<int>::Success(9));
    EXPECT_TRUE(scope->StopAndWait(1s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderArchitectureModuleCatalog, ResolvesDependenciesInStableOrder) {
    const auto catalog = BuiltinModuleCatalog::Create();
    const auto events = std::make_shared<std::vector<std::string>>();
    ASSERT_TRUE(catalog->Register(MakeTestModule("observer", {"processor"}, events)));
    ASSERT_TRUE(catalog->Register(MakeTestModule("source", {}, events)));
    ASSERT_TRUE(catalog->Register(MakeTestModule("processor", {"source"}, events)));

    const auto plan = catalog->ResolveStartupPlan();
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->size(), 3U);
    EXPECT_EQ((*plan)[0].descriptor.id, "source");
    EXPECT_EQ((*plan)[1].descriptor.id, "processor");
    EXPECT_EQ((*plan)[2].descriptor.id, "observer");
}

TEST(RenderArchitectureModuleCatalog, RejectsDependencyCyclesAndLateRegistration) {
    const auto catalog = BuiltinModuleCatalog::Create();
    const auto events = std::make_shared<std::vector<std::string>>();
    ASSERT_TRUE(catalog->Register(MakeTestModule("a", {"b"}, events)));
    ASSERT_TRUE(catalog->Register(MakeTestModule("b", {"a"}, events)));

    const auto plan = catalog->ResolveStartupPlan();
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error().code, RenderErrorCode::kModuleDependencyCycle);
    EXPECT_EQ(StableErrorCode(plan.error().code), "MODULE_DEPENDENCY_CYCLE");

    const auto late_registration = catalog->Register(MakeTestModule("late", {}, events));
    ASSERT_FALSE(late_registration.has_value());
    EXPECT_EQ(late_registration.error().code, RenderErrorCode::kModuleLifecycleRejected);
}

TEST(RenderArchitectureModuleCatalog, EnableControlIsIdempotentAndObservable) {
    const auto catalog = BuiltinModuleCatalog::Create();
    const auto events = std::make_shared<std::vector<std::string>>();
    ASSERT_TRUE(catalog->Register(MakeTestModule("debugger", {}, events)));

    EXPECT_TRUE(catalog->SetEnabled("debugger", true));
    EXPECT_TRUE(events->empty());
    EXPECT_TRUE(catalog->SetEnabled("debugger", false));
    EXPECT_EQ(*events, (std::vector<std::string>{"disable:debugger"}));
    EXPECT_TRUE(catalog->SetEnabled("debugger", false));
    EXPECT_EQ(events->size(), 1U);

    const auto snapshot = catalog->Snapshot("debugger");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FALSE(snapshot->enabled);
    const auto missing = catalog->SetEnabled("missing", true);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, RenderErrorCode::kModuleNotFound);
}

TEST(RenderArchitectureCompositionRoot, StartsDependenciesAndStopsInReverseOrder) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto catalog = BuiltinModuleCatalog::Create();
    auto root = RenderCompositionRoot::Create(runtime, catalog);
    ASSERT_TRUE(root);
    const auto events = std::make_shared<std::vector<std::string>>();
    ASSERT_TRUE(root->Register(MakeTestModule("observer", {"source"}, events)));
    ASSERT_TRUE(root->Register(MakeTestModule("source", {}, events)));

    const auto started = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto started_future = started->get_future();
    ASSERT_TRUE(root->RequestStart([started](ModuleLifecycleResult result) { started->set_value(std::move(result)); }));
    ASSERT_EQ(started_future.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(started_future.get());

    const auto duplicate = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto duplicate_future = duplicate->get_future();
    ASSERT_TRUE(root->RequestStart([duplicate](ModuleLifecycleResult result) { duplicate->set_value(std::move(result)); }));
    ASSERT_EQ(duplicate_future.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(duplicate_future.get());

    const auto stopped = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto stopped_future = stopped->get_future();
    ASSERT_TRUE(root->RequestStop([stopped](ModuleLifecycleResult result) { stopped->set_value(std::move(result)); }));
    ASSERT_EQ(stopped_future.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(stopped_future.get());
    EXPECT_EQ(*events, (std::vector<std::string>{"start:source", "start:observer", "stop:observer", "stop:source"}));

    root.reset();
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderArchitectureCompositionRoot, StartFailureRollsBackStartedModules) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto catalog = BuiltinModuleCatalog::Create();
    auto root = RenderCompositionRoot::Create(runtime, catalog);
    ASSERT_TRUE(root);
    const auto events = std::make_shared<std::vector<std::string>>();
    ASSERT_TRUE(root->Register(MakeTestModule("source", {}, events)));
    ASSERT_TRUE(root->Register(MakeTestModule(
        "observer", {"source"}, events, std::unexpected(MakeModuleTestError(RenderErrorCode::kModuleStartFailed, "start", "injected failure")))));

    const auto started = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto started_future = started->get_future();
    ASSERT_TRUE(root->RequestStart([started](ModuleLifecycleResult result) { started->set_value(std::move(result)); }));
    ASSERT_EQ(started_future.wait_for(5s), std::future_status::ready);
    const auto result = started_future.get();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::kModuleStartFailed);
    EXPECT_EQ(*events, (std::vector<std::string>{"start:source", "start:observer", "stop:source"}));
    const auto source = catalog->Snapshot("source");
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->runtime_state, BuiltinModuleRuntimeState::kStopped);

    root.reset();
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderArchitectureCompositionRoot, StopRequestedFromStartCallbackIsSafe) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto catalog = BuiltinModuleCatalog::Create();
    auto root = RenderCompositionRoot::Create(runtime, catalog);
    ASSERT_TRUE(root);
    const auto events = std::make_shared<std::vector<std::string>>();
    const auto stop_completion = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto stop_future = stop_completion->get_future();
    const std::weak_ptr<RenderCompositionRoot> weak_root = root;

    BuiltinModuleRegistration registration{
        .descriptor =
            BuiltinModuleDescriptor{
                .id = "callback_stop",
                .name = "Callback Stop",
                .author = "GammaRay",
                .description = "Stops the composition from its start callback",
                .version_name = "1.0.0",
                .version_code = 1,
                .capability = BuiltinModuleCapability::kObserver,
            },
        .start = [weak_root, events, stop_completion]() -> PxAwaitable<ModuleLifecycleResult> {
            events->push_back("start:callback_stop");
            if (const auto owner = weak_root.lock()) {
                static_cast<void>(
                    owner->RequestStop([stop_completion](ModuleLifecycleResult result) { stop_completion->set_value(std::move(result)); }));
            }
            co_return ModuleLifecycleResult{};
        },
        .stop = [events]() -> PxAwaitable<ModuleLifecycleResult> {
            events->push_back("stop:callback_stop");
            co_return ModuleLifecycleResult{};
        },
        .set_enabled = [](const bool) { return ModuleLifecycleResult{}; },
    };
    ASSERT_TRUE(root->Register(std::move(registration)));

    const auto start_completion = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto start_future = start_completion->get_future();
    ASSERT_TRUE(root->RequestStart([start_completion](ModuleLifecycleResult result) { start_completion->set_value(std::move(result)); }));

    ASSERT_EQ(start_future.wait_for(5s), std::future_status::ready);
    const auto start_result = start_future.get();
    ASSERT_FALSE(start_result.has_value());
    EXPECT_EQ(start_result.error().code, RenderErrorCode::kModuleLifecycleRejected);
    ASSERT_EQ(stop_future.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(stop_future.get());
    EXPECT_EQ(*events, (std::vector<std::string>{"start:callback_stop", "stop:callback_stop"}));

    root.reset();
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderArchitectureFrameDebugger, DisabledObserverRejectsWithoutQueueing) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    auto observer = FrameDebuggerObserver::Create(runtime);
    ASSERT_TRUE(observer);
    ASSERT_TRUE(observer->Start());

    EXPECT_EQ(observer->SubmitClientConnected(), FrameDebuggerSubmitResult::kDisabled);
    observer->ObserveRawFrame(RawVideoFrameObservation{
        .monitor_id = "monitor-a",
        .frame_index = 1,
        .width = 1920,
        .height = 1080,
    });
    auto snapshot = observer->Snapshot();
    EXPECT_TRUE(snapshot.running);
    EXPECT_FALSE(snapshot.enabled);
    EXPECT_EQ(snapshot.queue.accepted, 0U);
    EXPECT_EQ(snapshot.raw_frames_observed, 0U);

    ASSERT_TRUE(observer->SetEnabled(true));
    EXPECT_EQ(observer->SubmitClientConnected(), FrameDebuggerSubmitResult::kAccepted);
    observer->ObserveRawFrame(RawVideoFrameObservation{
        .monitor_id = "monitor-a",
        .frame_index = 2,
        .width = 1920,
        .height = 1080,
    });

    const auto completion = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto future = completion->get_future();
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    ASSERT_TRUE(scope->Spawn("stop_frame_debugger", [observer, completion]() {
        return CompleteFrameDebuggerStop(observer, std::chrono::steady_clock::now() + 1s, completion);
    }));
    ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(future.get());
    snapshot = observer->Snapshot();
    EXPECT_FALSE(snapshot.running);
    EXPECT_EQ(snapshot.raw_frames_observed, 1U);
    EXPECT_TRUE(scope->StopAndWait(1s));

    observer.reset();
    runtime->RequestStop();
    runtime->Join();
}

TEST(RenderArchitectureFrameDebugger, EncodedFramesDrainToOwnedFile) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output_directory = std::filesystem::temp_directory_path() / std::format("gammaray_frame_debugger_test_{}", unique_suffix);
    auto observer = FrameDebuggerObserver::Create(runtime, FrameDebuggerOptions{
                                                               .queue_capacity = 8,
                                                               .save_encoded_video = true,
                                                               .output_directory = output_directory,
                                                               .raw_log_interval = 1s,
                                                           });
    ASSERT_TRUE(observer);
    ASSERT_TRUE(observer->Start());
    ASSERT_TRUE(observer->SetEnabled(true));

    EXPECT_EQ(observer->SubmitEncoderReady(VideoEncoderReady{
                  .monitor_id = "DISPLAY_1",
                  .codec = "h264",
                  .width = 1280,
                  .height = 720,
              }),
              FrameDebuggerSubmitResult::kAccepted);
    EXPECT_EQ(observer->SubmitEncodedFrame(std::make_shared<const EncodedVideoFrame>(EncodedVideoFrame{
                  .identity =
                      FrameIdentity{
                          .stream_id = "stream-a",
                          .monitor_id = "DISPLAY_1",
                          .frame_index = 9,
                          .timestamp_us = 42,
                      },
                  .codec = "h264",
                  .key_frame = true,
                  .payload = MakePayload(4),
              })),
              FrameDebuggerSubmitResult::kAccepted);

    const auto completion = std::make_shared<std::promise<ModuleLifecycleResult>>();
    auto future = completion->get_future();
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    ASSERT_TRUE(scope->Spawn("drain_frame_debugger", [observer, completion]() {
        return CompleteFrameDebuggerStop(observer, std::chrono::steady_clock::now() + 1s, completion);
    }));
    ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(future.get());
    const auto snapshot = observer->Snapshot();
    EXPECT_EQ(snapshot.encoded_frames_submitted, 1U);
    EXPECT_EQ(snapshot.encoded_bytes_written, 4U);
    EXPECT_EQ(snapshot.file_write_failures, 0U);

    std::vector<std::filesystem::path> created_files;
    if (std::filesystem::exists(output_directory)) {
        for (const auto& entry : std::filesystem::directory_iterator(output_directory)) {
            if (entry.is_regular_file()) {
                created_files.push_back(entry.path());
            }
        }
    }
    EXPECT_EQ(created_files.size(), 1U);
    if (created_files.size() == 1U) {
        EXPECT_EQ(std::filesystem::file_size(created_files.front()), 4U);
    }
    std::error_code cleanup_error;
    for (const auto& file : created_files) {
        std::filesystem::remove(file, cleanup_error);
        EXPECT_FALSE(cleanup_error);
        cleanup_error.clear();
    }
    std::filesystem::remove(output_directory, cleanup_error);
    EXPECT_FALSE(cleanup_error);
    EXPECT_TRUE(scope->StopAndWait(1s));

    observer.reset();
    runtime->RequestStop();
    runtime->Join();
}

} // namespace
} // namespace px::render
