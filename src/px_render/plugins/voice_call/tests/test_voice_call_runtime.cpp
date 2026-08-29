#include "voice_call_runtime.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "px_message.pb.h"
#include "px_render/plugin_interface/px_plugin_events.h"

namespace px {
namespace {

std::shared_ptr<Message> MakeCallRequest(
    const std::string& stream_id,
    const std::string& call_id,
    uint64_t request_id) {
    auto message = std::make_shared<Message>();
    message->set_type(kVoiceCallRequest);
    message->set_device_id("controlled-device");
    message->set_stream_id(stream_id);
    auto& request = *message->mutable_voice_call_request();
    request.set_connect(true);
    request.set_call_id(call_id);
    request.set_request_id(request_id);
    return message;
}

TEST(VoiceCallRuntimeTest, RejectClosesConsentAndReturnsResponse) {
    const auto runtime = VoiceCallRuntime::Make(true, {});
    std::vector<std::shared_ptr<PxPluginBaseEvent>> events;
    runtime->SetEventDelivery(
        [&events](const std::shared_ptr<PxPluginBaseEvent>& event) {
            events.push_back(event);
        });
    runtime->OnClientConnected("visitor", "stream", "UDP");
    runtime->OnMessage(MakeCallRequest("stream", "call", 7));
    ASSERT_EQ(events.size(), 1u);
    const auto consent =
        std::dynamic_pointer_cast<PxPluginVoiceCallConsentEvent>(events.front());
    ASSERT_TRUE(consent);
    EXPECT_TRUE(consent->show_);

    MsgVoiceCallConsentDecision decision;
    decision.stream_id_ = "stream";
    decision.call_id_ = "call";
    decision.request_id_ = 7;
    decision.accepted_ = false;
    decision.reason_ = "rejected";
    runtime->ApplyConsentDecision(decision);

    ASSERT_EQ(events.size(), 3u);
    const auto cancel =
        std::dynamic_pointer_cast<PxPluginVoiceCallConsentEvent>(events[1]);
    ASSERT_TRUE(cancel);
    EXPECT_FALSE(cancel->show_);
    const auto response =
        std::dynamic_pointer_cast<PxPluginVoiceCallMediaEvent>(events[2]);
    ASSERT_TRUE(response);
    EXPECT_EQ(response->action_, PxVoiceCallMediaAction::kStreamMessage);
    EXPECT_TRUE(response->message_);
}

TEST(VoiceCallRuntimeTest, DeliveryCanUnregisterItselfDuringDispatch) {
    const auto runtime = VoiceCallRuntime::Make(true, {});
    const auto weak_runtime = std::weak_ptr<VoiceCallRuntime>(runtime);
    std::atomic_int deliveries = 0;
    runtime->SetEventDelivery(
        [weak_runtime, &deliveries](
            const std::shared_ptr<PxPluginBaseEvent>&) {
            ++deliveries;
            if (const auto active = weak_runtime.lock()) {
                active->ClearEventDelivery();
            }
        });
    runtime->OnClientConnected("visitor", "stream", "UDP");
    runtime->OnMessage(MakeCallRequest("stream", "call", 1));
    runtime->OnMessage(MakeCallRequest("stream", "other", 2));
    EXPECT_EQ(deliveries.load(), 1);
    runtime->Shutdown("test_complete");
}

TEST(VoiceCallRuntimeTest, DeliveryCanShutdownRuntimeDuringCallback) {
    const auto runtime = VoiceCallRuntime::Make(true, {});
    const auto weak_runtime = std::weak_ptr<VoiceCallRuntime>(runtime);
    std::atomic_int deliveries = 0;
    runtime->SetEventDelivery(
        [weak_runtime, &deliveries](
            const std::shared_ptr<PxPluginBaseEvent>&) {
            ++deliveries;
            if (const auto active = weak_runtime.lock()) {
                active->Shutdown("delivery_callback");
            }
        });
    runtime->OnClientConnected("visitor", "stream", "UDP");
    runtime->OnMessage(MakeCallRequest("stream", "call", 1));
    EXPECT_FALSE(runtime->IsAccepting());
    const int shutdown_deliveries = deliveries.load();
    EXPECT_GE(shutdown_deliveries, 1);
    runtime->OnMessage(MakeCallRequest("stream", "late", 2));
    EXPECT_EQ(deliveries.load(), shutdown_deliveries);
}

TEST(VoiceCallRuntimeTest, ExternalShutdownWaitsForInFlightDelivery) {
    const auto runtime = VoiceCallRuntime::Make(true, {});
    auto entered = std::make_shared<std::promise<void>>();
    auto entered_future = entered->get_future();
    auto release = std::make_shared<std::promise<void>>();
    const auto release_future = release->get_future().share();
    auto delivery_count = std::make_shared<std::atomic_int>(0);
    runtime->SetEventDelivery(
        [entered, release_future, delivery_count](
            const std::shared_ptr<PxPluginBaseEvent>&) {
            if (delivery_count->fetch_add(1) == 0) {
                entered->set_value();
                release_future.wait();
            }
        });
    runtime->OnClientConnected("visitor", "stream", "UDP");
    std::jthread producer([runtime] {
        runtime->OnMessage(MakeCallRequest("stream", "call", 1));
    });
    ASSERT_EQ(entered_future.wait_for(std::chrono::seconds(1)),
              std::future_status::ready);
    auto shutdown = std::async(std::launch::async, [runtime] {
        runtime->Shutdown("external_shutdown");
    });
    EXPECT_EQ(shutdown.wait_for(std::chrono::milliseconds(20)),
              std::future_status::timeout);
    release->set_value();
    EXPECT_EQ(shutdown.wait_for(std::chrono::seconds(1)),
              std::future_status::ready);
    producer.join();
}

TEST(VoiceCallRuntimeTest, TenRepeatedLifecyclesDropLateWork) {
    for (int round = 0; round < 10; ++round) {
        const auto runtime = VoiceCallRuntime::Make(true, {});
        std::atomic_int deliveries = 0;
        runtime->SetEventDelivery(
            [&deliveries](const std::shared_ptr<PxPluginBaseEvent>&) {
                ++deliveries;
            });
        const auto call_id = "call-" + std::to_string(round);
        runtime->OnClientConnected("visitor", "stream", "UDP");
        runtime->OnMessage(MakeCallRequest("stream", call_id, round + 1));
        EXPECT_EQ(deliveries.load(), 1) << "round " << round;
        runtime->Shutdown("round_complete");
        const int stopped_count = deliveries.load();
        runtime->OnMessage(
            MakeCallRequest("stream", "late-" + call_id, round + 100));
        runtime->On1Second();
        EXPECT_EQ(deliveries.load(), stopped_count) << "round " << round;
    }
}

}  // namespace
}  // namespace px
