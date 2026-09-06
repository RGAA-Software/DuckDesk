#include <memory>
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

TEST(JoystickServiceTest, RoutesRumbleToTheOriginatingTransport) {
    const auto state = std::make_shared<FakeJoystickState>();
    const auto backend = std::make_shared<FakeJoystickBackend>(state);
    std::string sent_transport;
    std::string sent_stream;
    std::uint64_t send_calls{0};
    Message sent_message;
    const auto service = JoystickService::Create(
        [backend] {
            return backend;
        },
        [&](const std::string& transport_id, const std::string& stream_id, const std::shared_ptr<Data>& data) {
            ++send_calls;
            sent_transport = transport_id;
            sent_stream = stream_id;
            return data && sent_message.ParseFromArray(data->Bytes().data(), static_cast<int>(data->Size()));
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
