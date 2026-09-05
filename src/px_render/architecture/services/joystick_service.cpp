#include "services/joystick_service.h"

#include <utility>

#include "px_common/log.h"
#include "px_message.pb.h"
#include "vigem_controller.h"

namespace px::render {
namespace {

class VigemJoystickBackend final : public JoystickBackend {
public:
    bool PrepareConnection() override {
        if (controller_ && !controller_->IsConnected()) {
            controller_->Exit();
            controller_.reset();
        }
        if (!controller_) {
            controller_ =
                std::make_shared<VigemController>(JoystickType::kJsX360);
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
        XInputGamepadState state;
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
    BackendFactory backend_factory) {
    if (!backend_factory) {
        backend_factory = [] {
            return std::make_shared<VigemJoystickBackend>();
        };
    }
    return std::make_shared<JoystickService>(std::move(backend_factory));
}

JoystickService::JoystickService(BackendFactory backend_factory)
    : backend_factory_(std::move(backend_factory)) {}

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
    const std::shared_ptr<Message>& message) {
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
    };
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
    if (!factory) {
        return {};
    }
    auto backend = factory();
    bool prepared = false;
    if (backend) {
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
