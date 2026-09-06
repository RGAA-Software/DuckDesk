//
// Created by RGAA on 2024/3/23.
//

#include "vigem_controller.h"
#include "px_common/log.h"

#include <Xinput.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace px
{
    namespace {
        struct RumbleCallbackContext final {
            std::string stream_id;
            VigemController::RumbleCallback callback;
        };

        std::mutex& CallbackRegistryMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<std::uintptr_t, std::shared_ptr<const RumbleCallbackContext>>& CallbackRegistry() {
            static std::unordered_map<std::uintptr_t, std::shared_ptr<const RumbleCallbackContext>> registry;
            return registry;
        }

        std::uintptr_t TargetKey(
            PVIGEM_TARGET target) { // NOLINT(gammaray-raw-pointer-boundary): transient ViGEm callback handle
            return reinterpret_cast<std::uintptr_t>(target);
        }

        void RegisterCallbackContext(
            const std::uintptr_t target_key,
            const std::shared_ptr<const RumbleCallbackContext>& context) {
            std::lock_guard lock(CallbackRegistryMutex());
            CallbackRegistry().insert_or_assign(target_key, context);
        }

        void UnregisterCallbackContext(const std::uintptr_t target_key) {
            std::lock_guard lock(CallbackRegistryMutex());
            CallbackRegistry().erase(target_key);
        }

        void CALLBACK OnX360Notification(
            PVIGEM_CLIENT,
            PVIGEM_TARGET target,
            UCHAR large_motor,
            UCHAR small_motor,
            UCHAR,
            LPVOID) { // NOLINT(gammaray-raw-pointer-boundary): ViGEm callback ABI
            std::shared_ptr<const RumbleCallbackContext> context;
            {
                std::lock_guard lock(CallbackRegistryMutex());
                const auto found = CallbackRegistry().find(TargetKey(target));
                if (found != CallbackRegistry().end()) {
                    context = found->second;
                }
            }
            if (context && context->callback) {
                context->callback(context->stream_id, large_motor, small_motor);
            }
        }
    }


    void VigemController::ClientDeleter::operator()(
        std::remove_pointer_t<PVIGEM_CLIENT>* client) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): ViGEm C handle boundary
        if (client) {
            vigem_free(client);
        }
    }

    void VigemController::TargetDeleter::operator()(
        std::remove_pointer_t<PVIGEM_TARGET>* target) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): ViGEm C handle boundary
        if (target) {
            vigem_target_free(target);
        }
    }

    VigemController::VigemController(const JoystickType &js_type, RumbleCallback rumble_callback)
        : js_type_(js_type), rumble_callback_(std::move(rumble_callback)) {
        LOGI("Joystick type: {}", (int) js_type_);
    }

    VigemController::~VigemController() {
        Exit();
    }

    bool VigemController::Connect() {
        if (client_) {
            return true;
        }
        client_.reset(vigem_alloc());
        if (!client_) {
            LOGE("Alloc vigem failed!!!");
            return false;
        }
        const auto ret = vigem_connect(client_.get());
        if (!VIGEM_SUCCESS(ret)) {
            LOGE("Connect failed !!!!");
            client_.reset();
            return false;
        }

        return true;
    }

    bool VigemController::IsConnected() {
        return client_ != nullptr;
    }

    bool VigemController::AllocController(const std::string &stream_id) {
        const auto existing = targets_.find(stream_id);
        if (existing != targets_.end() && existing->second && vigem_target_is_attached(existing->second.get())) {
            return true;
        }
        if (existing != targets_.end()) {
            static_cast<void>(RemoveController(stream_id));
        }

        TargetHandle target;
        if (js_type_ == JoystickType::kJsX360) {
            target.reset(vigem_target_x360_alloc());
        } else {
            target.reset(vigem_target_ds4_alloc());
        }
        if (!target) {
            LOGE("Alloc joystick failed for stream : {}", stream_id);
            return false;
        }

        auto err = vigem_target_add(client_.get(), target.get());
        if (!VIGEM_SUCCESS(err)) {
            LOGE("vigem_target_add joystick failed: 0x{:x}", (int32_t) err);
            return false;
        }

        const auto callback_context = std::make_shared<const RumbleCallbackContext>(RumbleCallbackContext{
            .stream_id = stream_id,
            .callback = rumble_callback_,
        });
        const auto target_key = TargetKey(target.get());
        RegisterCallbackContext(target_key, callback_context);
        err = vigem_target_x360_register_notification(
            client_.get(), target.get(), OnX360Notification, nullptr);

        if (!VIGEM_SUCCESS(err)) {
            UnregisterCallbackContext(target_key);
            LOGE("vigem_target_x360_register_notification x360 failed: 0x{:x}", (int32_t) err);
            vigem_target_remove(client_.get(), target.get());
            return false;
        }

        auto target_connected = vigem_target_is_attached(target.get());
        if (target_connected) {
            targets_.insert_or_assign(stream_id, std::move(target));
        } else {
            vigem_target_x360_unregister_notification(target.get());
            UnregisterCallbackContext(target_key);
        }
        LOGI("target connected: {}", target_connected);
        return target_connected;
    }

    bool VigemController::RemoveController(const std::string& stream_id) {
        const auto target = targets_.find(stream_id);
        if (target != targets_.end() && client_ && target->second) {
            vigem_target_x360_unregister_notification(target->second.get());
            UnregisterCallbackContext(TargetKey(target->second.get()));
            vigem_target_remove(client_.get(), target->second.get());
        }
        targets_.erase(stream_id);
        return true;
    }

    void VigemController::SendGamepadState(const std::string &stream_id, const XInputGamepadState &state) {
        const auto target = targets_.find(stream_id);
        if (target != targets_.end() && client_ && target->second) {
            XUSB_REPORT report{};
            static_assert(sizeof(report) == sizeof(state));
            std::memcpy(std::addressof(report), std::addressof(state), sizeof(report));
            vigem_target_x360_update(client_.get(), target->second.get(), report);
        }
    }

    void VigemController::Exit() {
        for (const auto &[stream_id, target]: targets_) {
            if (client_ && target) {
                vigem_target_x360_unregister_notification(target.get());
                UnregisterCallbackContext(TargetKey(target.get()));
                vigem_target_remove(client_.get(), target.get());
            }
        }
        targets_.clear();
        if (client_) {
            vigem_disconnect(client_.get());
            client_.reset();
        }
    }

    void VigemController::MockPressB() {
        //xbox
        // The XINPUT_GAMEPAD structure is identical to the XUSB_REPORT structure
        // so we can simply take it "as-is" and cast it.
        for (const auto &[stream_id, target]: targets_) {
            if (!client_ || !target) {
                continue;
            }
            XINPUT_GAMEPAD pad{};
            pad.wButtons |= XINPUT_GAMEPAD_X;
            pad.wButtons |= XINPUT_GAMEPAD_B;
            XUSB_REPORT report{};
            static_assert(sizeof(report) == sizeof(pad));
            std::memcpy(std::addressof(report), std::addressof(pad), sizeof(report));
            vigem_target_x360_update(client_.get(), target.get(), report);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            pad.wButtons &= ~XINPUT_GAMEPAD_X;
            pad.wButtons &= ~XINPUT_GAMEPAD_B;
            std::memcpy(std::addressof(report), std::addressof(pad), sizeof(report));
            vigem_target_x360_update(client_.get(), target.get(), report);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        }

    }
}
