#include "services/joystick_service.h"

#include <utility>

#include "px_common/log.h"
#include "px_message.pb.h"
#include "px_message/proto_converter.h"
#include "vigem_controller.h"

namespace px::render {
namespace {

class VigemJoystickBackend final : public JoystickBackend {
public:
    void SetRumbleCallback(RumbleCallback callback) override {
        rumble_callback_ = std::move(callback);
    }

    bool PrepareConnection() override {
        if (controller_ && !controller_->IsConnected()) {
            controller_->Exit();
            controller_.reset();
        }
        if (!controller_) {
            controller_ = std::make_shared<VigemController>(
                JoystickType::kJsX360, rumble_callback_);
        }
        return controller_->Connect();
    }

    bool AllocateController(const std::string& stream_id) override {
        if (!PrepareConnection() || !controller_ ||
            !controller_->IsConnected()) {
            return false;
        }
        return controller_->AllocController(stream_id);
    }

    void ReplayJoystickEvent(
        const std::string& stream_id,
        const std::shared_ptr<Message>& message) override {
        if (!controller_ || !message) {
            return;
        }
        const auto& gamepad_state = message->gamepad_state();
        XInputGamepadState state{};
        state.wButtons = gamepad_state.buttons();
        state.bLeftTrigger = gamepad_state.left_trigger();
        state.bRightTrigger = gamepad_state.right_trigger();
        state.sThumbLX = gamepad_state.thumb_lx();
        state.sThumbLY = gamepad_state.thumb_ly();
        state.sThumbRX = gamepad_state.thumb_rx();
        state.sThumbRY = gamepad_state.thumb_ry();
        controller_->SendGamepadState(stream_id, state);
    }

    void RemoveController(const std::string& stream_id) override {
        if (controller_) {
            static_cast<void>(controller_->RemoveController(stream_id));
        }
    }

    void Shutdown() override {
        if (controller_) {
            controller_->Exit();
            controller_.reset();
        }
    }

private:
    RumbleCallback rumble_callback_;
    std::shared_ptr<VigemController> controller_;
};

RenderError MakeJoystickError(std::string operation, std::string reason) {
    return RenderError{
        .code = RenderErrorCode::kModuleLifecycleRejected,
        .component = "joystick",
        .operation = std::move(operation),
        .stage = "service",
        .reason = std::move(reason),
        .recoverable = true,
    };
}

}  // namespace

std::shared_ptr<JoystickService> JoystickService::Create(
    BackendFactory backend_factory, SendCallback send_callback) {
    return std::make_shared<JoystickService>(
        std::move(backend_factory), std::move(send_callback));
}

JoystickService::JoystickService(
    BackendFactory backend_factory, SendCallback send_callback)
    : backend_factory_(std::move(backend_factory)),
      send_callback_(std::move(send_callback)) {}

JoystickService::~JoystickService() {
    static_cast<void>(Stop());
}

BuiltinModuleRegistration JoystickService::MakeRegistration() {
    const std::weak_ptr<JoystickService> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kJoystickModuleId),
            .name = "Joystick",
            .author = "GammaRay",
            .description = "Built-in per-stream ViGEm controller service",
            .version_name = "2.0.0",
            .version_code = 200,
            .capability = BuiltinModuleCapability::kService,
            .default_enabled = true,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner
                ? owner->Start()
                : ModuleLifecycleResult(std::unexpected(MakeJoystickError(
                      "start", "service owner expired")));
        },
        .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner ? owner->Stop() : ModuleLifecycleResult{};
        },
        .set_enabled = [weak_owner](const bool enabled) {
            const auto owner = weak_owner.lock();
            return owner
                ? owner->SetEnabled(enabled)
                : ModuleLifecycleResult(std::unexpected(MakeJoystickError(
                      "set_enabled", "service owner expired")));
        },
    };
}

ModuleLifecycleResult JoystickService::Start() {
    {
        std::lock_guard lock(mutex_);
        if (running_) {
            return {};
        }
        running_ = true;
    }
    const auto backend = EnsureBackend();
    LOGI("event=service.start component=joystick backend_ready={} outcome=success",
         backend != nullptr);
    return {};
}

ModuleLifecycleResult JoystickService::Stop() {
    std::shared_ptr<JoystickBackend> backend;
    {
        std::lock_guard lock(mutex_);
        if (!running_ && !backend_) {
            return {};
        }
        running_ = false;
        backend_ready_ = false;
        routes_.clear();
        backend = std::move(backend_);
    }
    if (backend) {
        std::lock_guard operation_lock(backend_operation_mutex_);
        backend->Shutdown();
    }
    LOGI("event=service.stop component=joystick outcome=success");
    return {};
}

ModuleLifecycleResult JoystickService::SetEnabled(const bool enabled) {
    std::shared_ptr<JoystickBackend> backend;
    bool should_create = false;
    {
        std::lock_guard lock(mutex_);
        enabled_ = enabled;
        if (!enabled) {
            backend_ready_ = false;
            routes_.clear();
            backend = std::move(backend_);
        }
        else {
            should_create = running_ && !backend_;
        }
    }
    if (backend) {
        std::lock_guard operation_lock(backend_operation_mutex_);
        backend->Shutdown();
    }
    if (should_create) {
        static_cast<void>(EnsureBackend());
    }
    return {};
}

void JoystickService::HandleMessage(
    const std::shared_ptr<Message>& message,
    const std::string& transport_id) {
    if (!message || (message->type() != MessageType::kHello &&
                     message->type() != MessageType::kGamepadState)) {
        return;
    }
    if (message->type() == MessageType::kHello &&
        !message->hello().enable_controller()) {
        return;
    }
    const auto backend = EnsureBackend();
    if (!backend) {
        std::lock_guard lock(mutex_);
        ++rejected_messages_;
        return;
    }
    const auto stream_id = message->stream_id();
    if (!stream_id.empty() && !transport_id.empty()) {
        std::lock_guard lock(mutex_);
        routes_.insert_or_assign(stream_id, transport_id);
    }
    if (message->type() == MessageType::kHello) {
        bool allocated = false;
        {
            std::lock_guard operation_lock(backend_operation_mutex_);
            allocated = backend->AllocateController(stream_id);
        }
        std::lock_guard lock(mutex_);
        if (allocated) {
            ++allocated_controllers_;
        }
        else {
            ++rejected_messages_;
            LOGE("event=joystick.allocate component=joystick outcome=failed "
                 "code=JOYSTICK_VIGEM_UNAVAILABLE operation=allocate_target "
                 "recoverable=true reason=vigem_unavailable");
        }
        return;
    }
    {
        std::lock_guard operation_lock(backend_operation_mutex_);
        backend->ReplayJoystickEvent(stream_id, message);
    }
    std::lock_guard lock(mutex_);
    ++replayed_events_;
}

void JoystickService::HandleClientDisconnected(
    const std::string& stream_id) {
    std::shared_ptr<JoystickBackend> backend;
    {
        std::lock_guard lock(mutex_);
        routes_.erase(stream_id);
        backend = backend_;
    }
    if (backend) {
        std::lock_guard operation_lock(backend_operation_mutex_);
        backend->RemoveController(stream_id);
    }
}

JoystickServiceSnapshot JoystickService::Snapshot() const {
    std::lock_guard lock(mutex_);
    return JoystickServiceSnapshot{
        .running = running_,
        .enabled = enabled_,
        .backend_ready = backend_ready_,
        .allocated_controllers = allocated_controllers_,
        .replayed_events = replayed_events_,
        .rejected_messages = rejected_messages_,
        .rumble_events = rumble_events_,
        .rumble_send_failures = rumble_send_failures_,
    };
}

void JoystickService::HandleRumble(
    const std::string& stream_id,
    const std::uint8_t strong_motor,
    const std::uint8_t weak_motor) {
    SendCallback sender;
    std::string transport_id;
    {
        std::lock_guard lock(mutex_);
        if (!running_ || !enabled_) {
            return;
        }
        ++rumble_events_;
        const auto route = routes_.find(stream_id);
        if (route != routes_.end()) {
            transport_id = route->second;
        }
        sender = send_callback_;
    }
    bool sent = false;
    if (sender && !transport_id.empty() && !stream_id.empty()) {
        Message message;
        message.set_type(MessageType::kGamepadRumble);
        message.set_stream_id(stream_id);
        auto& rumble = *message.mutable_gamepad_rumble();
        rumble.set_strong_motor(strong_motor);
        rumble.set_weak_motor(weak_motor);
        sent = sender(
            transport_id, stream_id,
            ProtoAsData(&message)); // NOLINT(gammaray-raw-pointer-boundary): synchronous protobuf conversion
    }
    if (!sent) {
        std::lock_guard lock(mutex_);
        ++rumble_send_failures_;
    }
}

std::shared_ptr<JoystickBackend> JoystickService::EnsureBackend() {
    BackendFactory factory;
    {
        std::lock_guard lock(mutex_);
        if (!running_ || !enabled_) {
            return {};
        }
        if (backend_) {
            return backend_;
        }
        factory = backend_factory_;
    }
    auto backend = factory
                       ? factory()
                       : std::make_shared<VigemJoystickBackend>();
    bool prepared = false;
    if (backend) {
        const std::weak_ptr<JoystickService> weak_owner = weak_from_this();
        backend->SetRumbleCallback(
            [weak_owner](
                const std::string& stream_id,
                const std::uint8_t strong_motor,
                const std::uint8_t weak_motor) {
                if (const auto owner = weak_owner.lock()) {
                    owner->HandleRumble(
                        stream_id, strong_motor, weak_motor);
                }
            });
        std::lock_guard operation_lock(backend_operation_mutex_);
        prepared = backend->PrepareConnection();
    }
    if (!backend || !prepared) {
        LOGE("event=joystick.connect component=joystick outcome=failed "
             "code=JOYSTICK_VIGEM_UNAVAILABLE operation=connect_bus "
             "recoverable=true reason=vigem_unavailable");
        return {};
    }
    {
        std::lock_guard lock(mutex_);
        if (!running_ || !enabled_) {
            std::lock_guard operation_lock(backend_operation_mutex_);
            backend->Shutdown();
            return {};
        }
        if (!backend_) {
            backend_ = backend;
            backend_ready_ = true;
        }
        return backend_;
    }
}

}  // namespace px::render
