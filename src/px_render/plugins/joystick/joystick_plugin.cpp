//
// Created RGAA on 15/11/2024.
//

#include "joystick_plugin.h"
#include "px_message.pb.h"
#include "px_common_new/log.h"
#include "px_common_new/file.h"
#include "vigem/vigem_controller.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugin_interface/px_plugin_context.h"

#include <mutex>

PX_PLUGIN_EXPORT(px::JoystickPlugin)

namespace px
{
    class JoystickRuntime final {
    public:
        void PrepareConnection() {
            std::lock_guard lock(mutex_);
            if (controller_ && !controller_->IsConnected()) {
                controller_->Exit();
                controller_.reset();
            }
            if (!controller_) {
                controller_ =
                    std::make_shared<VigemController>(JoystickType::kJsX360);
            }
            if (!controller_->Connect()) {
                LOGE("Connect VIGEM failed!");
                return;
            }
            LOGI("Connect VIGEM success.");
        }

        void AllocateController(const std::string& stream_id) {
            PrepareConnection();
            std::lock_guard lock(mutex_);
            if (controller_ && controller_->IsConnected()
                && !controller_->AllocController(stream_id)) {
                LOGE("Alloc controller failed: {}", stream_id);
            }
        }

        void ReplayJoystickEvent(
            const std::string& stream_id,
            const std::shared_ptr<Message>& msg) {
            const auto& gamepad_state = msg->gamepad_state();
            XInputGamepadState state;
            state.wButtons = gamepad_state.buttons();
            state.bLeftTrigger = gamepad_state.left_trigger();
            state.bRightTrigger = gamepad_state.right_trigger();
            state.sThumbLX = gamepad_state.thumb_lx();
            state.sThumbLY = gamepad_state.thumb_ly();
            state.sThumbRX = gamepad_state.thumb_rx();
            state.sThumbRY = gamepad_state.thumb_ry();

            std::lock_guard lock(mutex_);
            if (controller_) {
                controller_->SendGamepadState(stream_id, state);
            }
        }

        void RemoveController(const std::string& stream_id) {
            std::lock_guard lock(mutex_);
            if (controller_) {
                controller_->RemoveController(stream_id);
            }
        }

    private:
        std::mutex mutex_;
        std::shared_ptr<VigemController> controller_;
    };

    std::string JoystickPlugin::GetPluginId() {
        return kJoystickPluginId;
    }

    std::string JoystickPlugin::GetPluginName() {
        return "JoyStick";
    }

    std::string JoystickPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t JoystickPlugin::GetVersionCode() {
        return 110;
    }

    std::string JoystickPlugin::GetPluginDescription() {
        return plugin_desc_;
    }

    void JoystickPlugin::On1Second() {
        PxPluginInterface::On1Second();

    }
    
    bool JoystickPlugin::OnCreate(const px::PxPluginParam &param) {
        PxPluginInterface::OnCreate(param);

        if (!IsPluginEnabled()) {
            return true;
        }
        runtime_ = std::make_shared<JoystickRuntime>();
        runtime_->PrepareConnection();

        return true;
    }

    bool JoystickPlugin::OnDestroy() {
        PxPluginInterface::OnStop();
        runtime_.reset();
        return PxPluginInterface::OnDestroy();
    }

    void JoystickPlugin::OnMessage(std::shared_ptr<Message> msg) {
        PxPluginInterface::OnMessage(msg);
        auto stream_id = msg->stream_id();
        if (msg->type() == px::MessageType::kHello) {
            auto sub = msg->hello();
            if (sub.enable_controller()) {
                const auto runtime = runtime_;
                plugin_context_->PostWorkTask([runtime, stream_id]() {
                    if (runtime) {
                        runtime->AllocateController(stream_id);
                    }
                });
            }
        }
        else if (msg->type() == px::MessageType::kGamepadState) {
            // replay gamepad state
            const auto runtime = runtime_;
            plugin_context_->PostWorkTask([runtime, stream_id, msg = std::move(msg)]() {
                if (runtime) {
                    runtime->ReplayJoystickEvent(stream_id, msg);
                }
            });
        }
    }

    void JoystickPlugin::OnClientDisconnected(const std::string &visitor_device_id, const std::string &stream_id) {
        LOGW("will release joystick controller for stream: {}, device id: {}", stream_id, visitor_device_id);
        const auto runtime = runtime_;
        if (runtime) {
            runtime->RemoveController(stream_id);
        }
    }

}
