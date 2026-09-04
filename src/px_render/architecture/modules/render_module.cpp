#include "render_module.h"

#include <format>
#include <mutex>
#include <utility>

#include "px_common_new/folder_util.h"
#include "px_common_new/log.h"
#include "px_common_new/memory_stat.h"
#include "px_common_new/snowflake_id.h"
#include "px_common_new/string_util.h"
#include "px_render/plugin_interface/px_plugin_context.h"

namespace px {

class ModuleEventChannel final {
public:
    void Set(const CompatibilityEventCallback& callback) {
        const std::lock_guard lock(mutex_);
        callback_ = callback;
        accepting_ = static_cast<bool>(callback_);
    }

    void Deactivate() {
        const std::lock_guard lock(mutex_);
        accepting_ = false;
        callback_ = {};
    }

    [[nodiscard]] bool CanDeliver() const {
        const std::lock_guard lock(mutex_);
        return accepting_ && static_cast<bool>(callback_);
    }

    void Deliver(const std::shared_ptr<PxPluginBaseEvent>& event) const {
        CompatibilityEventCallback callback;
        {
            const std::lock_guard lock(mutex_);
            if (!accepting_ || !callback_) {
                return;
            }
            callback = callback_;
        }
        callback(event);
    }

private:
    mutable std::mutex mutex_;
    CompatibilityEventCallback callback_;
    bool accepting_{false};
};

RenderModule::RenderModule()
    : event_channel_(std::make_shared<ModuleEventChannel>()) {}

std::shared_ptr<PxPluginContext> RenderModule::Context() const {
    return module_context_;
}

std::string RenderModule::Name() const { return Id(); }
std::string RenderModule::Author() const { return "RGAA"; }
std::string RenderModule::Description() const { return {}; }
std::string RenderModule::VersionName() const { return "1.0.0"; }
std::uint32_t RenderModule::VersionCode() const { return 1; }

bool RenderModule::IsEnabled() const noexcept { return enabled_.load(); }

void RenderModule::SetEnabled(const bool enabled) noexcept {
    enabled_.store(enabled);
}

bool RenderModule::IsWorking() const { return IsEnabled(); }

bool RenderModule::Start(const RenderModuleConfiguration& configuration) {
    if (lifecycle_.load() == RenderModuleLifecycle::kRunning) {
        return true;
    }
    if (lifecycle_.load() == RenderModuleLifecycle::kDestroyed) {
        return false;
    }

    SnowflakeId::initialize(0, 103);
    MemoryStat::Instance();
    configuration_ = configuration;
    module_context_ = std::make_shared<PxPluginContext>(Name());
    const auto log_path = std::format(
        L"{}/px_logs/{}.log", configuration_.base_data_path,
        StringUtil::ToWString(configuration_.instance_name));
    Logger::InitLog(log_path, true);

    settings_.device_id = configuration_.device_id;
    settings_.direct_allow_takeover = configuration_.direct_allow_takeover;
    settings_.relay_enabled = configuration_.relay_enabled;
    settings_.relay_host = configuration_.relay_host;
    settings_.relay_port = configuration_.relay_port;
    settings_.language = configuration_.language;
    settings_.appkey = configuration_.appkey;

    stopped_.store(false);
    destroyed_.store(false);
    lifecycle_.store(RenderModuleLifecycle::kRunning);
    LOGI("event=module.lifecycle component={} operation=start outcome=success", Id());
    return true;
}

bool RenderModule::Resume() {
    if (lifecycle_.load() == RenderModuleLifecycle::kDestroyed) {
        return false;
    }
    stopped_.store(false);
    lifecycle_.store(RenderModuleLifecycle::kRunning);
    return true;
}

bool RenderModule::Stop() {
    if (lifecycle_.load() == RenderModuleLifecycle::kDestroyed) {
        return true;
    }
    lifecycle_.store(RenderModuleLifecycle::kStopping);
    stopped_.store(true);
    event_channel_->Deactivate();
    return true;
}

bool RenderModule::Destroy() {
    if (lifecycle_.load() == RenderModuleLifecycle::kDestroyed) {
        return true;
    }
    Stop();
    if (module_context_) {
        module_context_->OnDestroy();
        module_context_.reset();
    }
    destroyed_.store(true);
    lifecycle_.store(RenderModuleLifecycle::kDestroyed);
    return true;
}

bool RenderModule::IsStoppingOrDestroyed() const noexcept {
    const auto lifecycle = lifecycle_.load();
    return stopped_.load() || destroyed_.load() ||
        lifecycle == RenderModuleLifecycle::kStopping ||
        lifecycle == RenderModuleLifecycle::kDestroyed;
}

void RenderModule::PostWorkTask(std::function<void()>&& task) {
    if (module_context_ && !IsStoppingOrDestroyed()) {
        module_context_->PostWorkTask(std::move(task));
    }
}

void RenderModule::PostUiTask(std::function<void()>&& task) {
    if (!IsStoppingOrDestroyed() && task) {
        task();
    }
}

void RenderModule::PostDelayedUiTask(
    const int milliseconds, std::function<void()>&& task) {
    if (module_context_ && !IsStoppingOrDestroyed()) {
        module_context_->PostDelayTask(std::move(task), milliseconds);
    }
}

void RenderModule::SetCompatibilityEventCallback(
    const CompatibilityEventCallback& callback) {
    event_channel_->Set(callback);
}

void RenderModule::EmitCompatibilityEvent(
    const std::shared_ptr<PxPluginBaseEvent>& event) {
    if (!event || IsStoppingOrDestroyed() || !event_channel_->CanDeliver()) {
        return;
    }
    event->plugin_name_ = Id();
    const std::weak_ptr<ModuleEventChannel> weak_channel = event_channel_;
    PostWorkTask([weak_channel, event] {
        if (const auto channel = weak_channel.lock()) {
            channel->Deliver(event);
        }
    });
}

void RenderModule::EmitCompatibilityEventImmediately(
    const std::shared_ptr<PxPluginBaseEvent>& event) {
    if (event && !IsStoppingOrDestroyed()) {
        event->plugin_name_ = Id();
        event_channel_->Deliver(event);
    }
}

CompatibilityEventCallback
RenderModule::MakeImmediateCompatibilityEventDispatcher() const {
    const std::weak_ptr<ModuleEventChannel> weak_channel = event_channel_;
    return [weak_channel](const std::shared_ptr<PxPluginBaseEvent>& event) {
        if (event) {
            if (const auto channel = weak_channel.lock()) {
                channel->Deliver(event);
            }
        }
    };
}

void RenderModule::Tick1Second() {}

void RenderModule::RequestKeyFrame() {
    EmitCompatibilityEvent(std::make_shared<PxPluginInsertIdrEvent>());
}

void RenderModule::HandleCommand(const std::string&) {}

void RenderModule::OnClientConnected(
    const std::string&, const std::string&, const std::string&) {
    no_connected_clients_counter_.store(0);
}

void RenderModule::OnClientDisconnected(
    const std::string&, const std::string&) {}

void RenderModule::HandleMessage(const std::shared_ptr<Message>&) {}

void RenderModule::UpdateSettings(const RenderModuleSettings& settings) {
    settings_ = settings;
}

void RenderModule::HandleAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
    if (!event || event->type_ != AppBaseEvent::EType::kConnectedClientCount) {
        return;
    }
    const auto connected = std::dynamic_pointer_cast<MsgConnectedClientCount>(event);
    if (connected && connected->connected_client_count_ <= 0) {
        ++no_connected_clients_counter_;
    }
    else {
        no_connected_clients_counter_.store(0);
    }
}

void RenderModule::UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage&) {}

RenderModuleSettings RenderModule::Settings() const { return settings_; }

bool RenderModule::HasNoConnectedClients() const noexcept {
    return no_connected_clients_counter_.load() > 10;
}

void RenderModule::ReportDataSent(const std::size_t bytes) {
    const auto event = std::make_shared<PxPluginDataSent>();
    event->size_ = static_cast<int>(bytes);
    EmitCompatibilityEvent(event);
}

void RenderModule::UpdateD3DResources(
    const std::uint64_t adapter_uid,
    const Microsoft::WRL::ComPtr<ID3D11Device>& device,
    const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context) {
    d3d11_devices_[adapter_uid] = device;
    d3d11_device_contexts_[adapter_uid] = context;
}

void RenderModule::ClearD3DResources(const std::uint64_t adapter_uid) {
    d3d11_devices_.erase(adapter_uid);
    d3d11_device_contexts_.erase(adapter_uid);
}

}  // namespace px
