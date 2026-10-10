#include <memory>
#include <future>
#include <chrono>
#include <thread>
#include <string>

#include <gtest/gtest.h>

#include "px_common/data.h"
#include "px_message.pb.h"
#include "services/joystick_service.h"

namespace px::render {
namespace {

struct FakeJoystickState final {
    bool prepared{true};
    std::uint64_t prepare_calls{0};
    std::uint64_t allocate_calls{0};
    std::uint64_t replay_calls{0};
    std::uint64_t remove_calls{0};
    std::uint64_t shutdown_calls{0};
    std::string last_stream_id;
    std::function<void()> before_allocate{};
    std::function<void()> on_shutdown{};
};

class FakeJoystickBackend final : public JoystickBackend {
public:
    explicit FakeJoystickBackend(std::shared_ptr<FakeJoystickState> state)
        : state_(std::move(state)) {}

    void SetRumbleCallback(RumbleCallback callback) override {
        rumble_callback_ = std::move(callback);
    }

    bool PrepareConnection() override {
        ++state_->prepare_calls;
        return state_->prepared;
    }

    bool AllocateController(const std::string& stream_id) override {
        if (state_->before_allocate) state_->before_allocate();
        ++state_->allocate_calls;
        state_->last_stream_id = stream_id;
        return state_->prepared;
    }

    void ReplayJoystickEvent(
        const std::string& stream_id,
        const std::shared_ptr<Message>&) override {
        ++state_->replay_calls;
        state_->last_stream_id = stream_id;
    }

    void RemoveController(const std::string& stream_id) override {
        ++state_->remove_calls;
        state_->last_stream_id = stream_id;
    }

    void Shutdown() override {
        ++state_->shutdown_calls;
        if (state_->on_shutdown) state_->on_shutdown();
    }

    void TriggerRumble(
        const std::string& stream_id,
        const std::uint8_t strong_motor,
        const std::uint8_t weak_motor) const {
        if (rumble_callback_) {
            rumble_callback_(stream_id, strong_motor, weak_motor);
        }
    }

private:
    std::shared_ptr<FakeJoystickState> state_;
    RumbleCallback rumble_callback_;
};

std::shared_ptr<Message> MakeHello(const std::string& stream_id) {
    auto message = std::make_shared<Message>();
    message->set_type(MessageType::kHello);
    message->set_stream_id(stream_id);
    message->mutable_hello()->set_enable_controller(true);
    return message;
}

std::shared_ptr<Message> MakeGamepad(const std::string& stream_id) {
    auto message = std::make_shared<Message>();
    message->set_type(MessageType::kGamepadState);
    message->set_stream_id(stream_id);
    message->mutable_gamepad_state()->set_buttons(1);
    return message;
}

TEST(JoystickServiceTest, RoutesTypedMessagesAndDisconnect) {
    const auto state = std::make_shared<FakeJoystickState>();
    const auto service = JoystickService::Create([state] {
        return std::make_shared<FakeJoystickBackend>(state);
    });

    ASSERT_TRUE(service->Start());
    service->HandleMessage(MakeHello("stream-a"));
    service->HandleMessage(MakeGamepad("stream-a"));
    service->HandleClientDisconnected("stream-a");

    const auto snapshot = service->Snapshot();
    EXPECT_TRUE(snapshot.running);
    EXPECT_TRUE(snapshot.backend_ready);
    EXPECT_EQ(snapshot.allocated_controllers, 1U);
    EXPECT_EQ(snapshot.replayed_events, 1U);
    EXPECT_EQ(state->allocate_calls, 1U);
    EXPECT_EQ(state->replay_calls, 1U);
    EXPECT_EQ(state->remove_calls, 1U);
    EXPECT_EQ(state->last_stream_id, "stream-a");
    ASSERT_TRUE(service->Stop());
    EXPECT_EQ(state->shutdown_calls, 1U);
}

TEST(JoystickServiceTest, DisableRejectsAndRepeatedLifecycleIsSafe) {
    const auto state = std::make_shared<FakeJoystickState>();
    const auto service = JoystickService::Create([state] {
        return std::make_shared<FakeJoystickBackend>(state);
    });

    for (int round = 0; round < 10; ++round) {
        ASSERT_TRUE(service->Start());
        ASSERT_TRUE(service->SetEnabled(false));
        service->HandleMessage(MakeGamepad("disabled"));
        EXPECT_FALSE(service->Snapshot().backend_ready);
        ASSERT_TRUE(service->SetEnabled(true));
        service->HandleMessage(MakeHello("enabled"));
        ASSERT_TRUE(service->Stop());
        ASSERT_TRUE(service->Stop());
    }
    EXPECT_EQ(state->replay_calls, 0U);
    EXPECT_EQ(state->allocate_calls, 10U);
    EXPECT_EQ(state->shutdown_calls, 20U);
}

TEST(JoystickServiceTest, MissingDriverIsIsolatedFromComposition) {
    const auto state = std::make_shared<FakeJoystickState>();
    state->prepared = false;
    const auto service = JoystickService::Create([state] {
        return std::make_shared<FakeJoystickBackend>(state);
    });

    ASSERT_TRUE(service->Start());
    service->HandleMessage(MakeHello("unavailable"));
    const auto snapshot = service->Snapshot();
    EXPECT_TRUE(snapshot.running);
    EXPECT_FALSE(snapshot.backend_ready);
    EXPECT_EQ(snapshot.rejected_messages, 1U);
    EXPECT_EQ(snapshot.allocated_controllers, 0U);
    ASSERT_TRUE(service->Stop());
}

TEST(JoystickServiceTest, SlowDriverDoesNotBlockNetworkQueueAndDisconnectCancelsPendingInput) {
    using namespace std::chrono_literals;
    const auto state = std::make_shared<FakeJoystickState>();
    const auto entered = std::make_shared<std::promise<void>>();
    auto entered_future = entered->get_future();
    const auto release = std::make_shared<std::promise<void>>();
    const auto released = release->get_future().share();
    state->before_allocate = [entered, released] {
        entered->set_value();
        static_cast<void>(released.wait_for(2s));
    };
    const auto service = JoystickService::Create([state] { return std::make_shared<FakeJoystickBackend>(state); });
    ASSERT_TRUE(service->Start());
    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(service->QueueMessage(MakeHello("slow-driver"), "iroh"));
    EXPECT_LT(std::chrono::steady_clock::now() - started, 500ms);
    ASSERT_EQ(entered_future.wait_for(1s), std::future_status::ready);
    ASSERT_TRUE(service->QueueMessage(MakeGamepad("slow-driver"), "iroh"));
    for (int pending_index{}; pending_index < 127; ++pending_index) {
        ASSERT_TRUE(service->QueueMessage(MakeGamepad("slow-driver"), "iroh"));
    }
    EXPECT_FALSE(service->QueueMessage(MakeGamepad("slow-driver"), "iroh"));
    auto disconnected = std::async(std::launch::async, [service] { service->HandleClientDisconnected("slow-driver"); });
    // A stopped service cancels queued messages even while an active driver call finishes.
    auto stopped = std::async(std::launch::async, [service] { return service->Stop(); });
    const auto stop_deadline = std::chrono::steady_clock::now() + 1s;
    while (service->Snapshot().running && std::chrono::steady_clock::now() < stop_deadline) std::this_thread::yield();
    EXPECT_FALSE(service->Snapshot().running);
    release->set_value();
    ASSERT_EQ(stopped.wait_for(3s), std::future_status::ready);
    ASSERT_TRUE(stopped.get());
    ASSERT_EQ(disconnected.wait_for(3s), std::future_status::ready);
    disconnected.get();
    EXPECT_EQ(state->replay_calls, 0U);
    ASSERT_TRUE(service->Start());
    EXPECT_FALSE(service->QueueMessage(MakeGamepad("slow-driver"), "iroh"));
    ASSERT_TRUE(service->Stop());
}

TEST(JoystickServiceTest, QueuedDriverCallbackCanStopItsService) {
    using namespace std::chrono_literals;
    const auto state = std::make_shared<FakeJoystickState>();
    const auto shutdown = std::make_shared<std::promise<void>>();
    auto shutdown_completed = shutdown->get_future();
    state->on_shutdown = [shutdown] { shutdown->set_value(); };
    const auto service = JoystickService::Create([state] { return std::make_shared<FakeJoystickBackend>(state); });
    state->before_allocate = [owner = std::weak_ptr<JoystickService>{service}] {
        if (const auto active = owner.lock()) static_cast<void>(active->Stop());
    };
    ASSERT_TRUE(service->Start());
    ASSERT_TRUE(service->QueueMessage(MakeHello("callback-stop"), "iroh"));
    ASSERT_EQ(shutdown_completed.wait_for(3s), std::future_status::ready);
    EXPECT_FALSE(service->Snapshot().running);
    ASSERT_TRUE(service->Stop());
}

TEST(JoystickServiceTest, ReleasingOwnerDuringDriverCallCancelsQueuedMessages) {
    using namespace std::chrono_literals;
    const auto state = std::make_shared<FakeJoystickState>();
    const auto entered = std::make_shared<std::promise<void>>();
    auto entered_future = entered->get_future();
    const auto release = std::make_shared<std::promise<void>>();
    const auto released = release->get_future().share();
    const auto shutdown = std::make_shared<std::promise<void>>();
    auto shutdown_completed = shutdown->get_future();
    state->on_shutdown = [shutdown] { shutdown->set_value(); };
    state->before_allocate = [entered, released] {
        entered->set_value();
        static_cast<void>(released.wait_for(2s));
    };
    auto service = JoystickService::Create([state] { return std::make_shared<FakeJoystickBackend>(state); });
    ASSERT_TRUE(service->Start());
    ASSERT_TRUE(service->QueueMessage(MakeHello("owner-release"), "iroh"));
    ASSERT_EQ(entered_future.wait_for(1s), std::future_status::ready);
    ASSERT_TRUE(service->QueueMessage(MakeGamepad("owner-release"), "iroh"));
    service.reset();
    release->set_value();
    ASSERT_EQ(shutdown_completed.wait_for(3s), std::future_status::ready);
    EXPECT_EQ(state->replay_calls, 0U);
}

TEST(JoystickServiceTest, RoutesRumbleToTheOriginatingTransport) {
    const auto state = std::make_shared<FakeJoystickState>();
    const auto backend = std::make_shared<FakeJoystickBackend>(state);
    std::string sent_transport;
    std::string sent_stream;
    std::uint64_t send_calls{0};
    Message sent_message;
    const auto service =
        JoystickService::Create([backend] { return backend; },
                                [&](const std::string& transport_id, const std::string& stream_id, const std::shared_ptr<Data>& payload) {
                                    ++send_calls;
                                    sent_transport = transport_id;
                                    sent_stream = stream_id;
                                    return payload && sent_message.ParseFromArray(payload->Bytes().data(), static_cast<int>(payload->Size()));
                                });

    ASSERT_TRUE(service->Start());
    service->HandleMessage(MakeHello("stream-rumble"), "ws-transport");
    backend->TriggerRumble("stream-rumble", 201U, 73U);

    EXPECT_EQ(sent_transport, "ws-transport");
    EXPECT_EQ(sent_stream, "stream-rumble");
    EXPECT_EQ(sent_message.type(), MessageType::kGamepadRumble);
    ASSERT_TRUE(sent_message.has_gamepad_rumble());
    EXPECT_EQ(sent_message.gamepad_rumble().strong_motor(), 201U);
    EXPECT_EQ(sent_message.gamepad_rumble().weak_motor(), 73U);
    const auto snapshot = service->Snapshot();
    EXPECT_EQ(snapshot.rumble_events, 1U);
    EXPECT_EQ(snapshot.rumble_send_failures, 0U);

    service->HandleClientDisconnected("stream-rumble");
    backend->TriggerRumble("stream-rumble", 255U, 255U);
    ASSERT_TRUE(service->Stop());
    backend->TriggerRumble("stream-rumble", 255U, 255U);
    EXPECT_EQ(send_calls, 1U);
    EXPECT_EQ(service->Snapshot().rumble_events, 2U);
    EXPECT_EQ(service->Snapshot().rumble_send_failures, 1U);
}

}  // namespace
}  // namespace px::render
