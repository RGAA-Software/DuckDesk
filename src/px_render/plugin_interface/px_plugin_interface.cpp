//
// Created by RGAA on 15/11/2024.
//

#include "px_plugin_interface.h"
#include "px_common_new/image.h"
#include "px_common_new/data.h"
#include "px_common_new/thread.h"
#include "px_plugin_events.h"
#include "px_common_new/log.h"
#include "px_common_new/memory_stat.h"
#include "px_plugin_context.h"
#include "px_common_new/snowflake_id.h"
#include <px_common_new/string_util.h>
#include "px_common_new/time_util.h"
#include <atomic>

extern "C"
{

#ifdef WIN32
    BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
        switch (ul_reason_for_call) {
            case DLL_PROCESS_ATTACH:
                LOGI("Attach to process.");
                break;
            case DLL_THREAD_ATTACH:
                //LOGI("Attach to thread: {}", GetCurrentThreadId());
                break;
            case DLL_THREAD_DETACH:
                //LOGI("Detach from thread: {}", GetCurrentThreadId());
                break;
            case DLL_PROCESS_DETACH:
                break;
        }
        return TRUE;
    }
#endif
}

namespace px
{

    class PxPluginEventChannel {
    public:
        void Register(const PxPluginEventCallback& callback) {
            std::lock_guard lock(mutex_);
            callback_ = callback;
            accepting_ = static_cast<bool>(callback_);
        }

        void Deactivate() {
            std::lock_guard lock(mutex_);
            accepting_ = false;
            callback_ = nullptr;
        }

        [[nodiscard]] bool CanDeliver() const {
            std::lock_guard lock(mutex_);
            return accepting_ && static_cast<bool>(callback_);
        }

        void Deliver(const std::shared_ptr<PxPluginBaseEvent>& event) const {
            PxPluginEventCallback callback;
            {
                std::lock_guard lock(mutex_);
                if (!accepting_ || !callback_) {
                    return;
                }
                callback = callback_;
            }
            // Invoke outside the mutex. A callback is allowed to unregister
            // itself or stop the plug-in without deadlocking delivery.
            callback(event);
        }

    private:
        mutable std::mutex mutex_;
        PxPluginEventCallback callback_;
        bool accepting_ = false;
    };

    PxPluginInterface::PxPluginInterface()
        : event_channel_(std::make_shared<PxPluginEventChannel>()) {
    }

    std::shared_ptr<PxPluginContext> PxPluginInterface::GetPluginContext() {
        return plugin_context_;
    }

    std::string PxPluginInterface::GetPluginName() {
        return "dummy";
    }

    std::string PxPluginInterface::GetPluginAuthor() {
        return plugin_author_;
    }

    std::string PxPluginInterface::GetPluginDescription() {
        return "plugin description";
    }

    PxPluginType PxPluginInterface::GetPluginType() {
        return plugin_type_;
    }

    bool PxPluginInterface::IsStreamPlugin() {
        return plugin_type_ == PxPluginType::kStream;
    }

    std::string PxPluginInterface::GetVersionName() {
        return "1.0.0";
    }

    uint32_t PxPluginInterface::GetVersionCode() {
        return 1;
    }

    bool PxPluginInterface::IsPluginEnabled() {
        return plugin_enabled_;
    }

    void PxPluginInterface::EnablePlugin() {
        plugin_enabled_ = true;
    }

    void PxPluginInterface::DisablePlugin() {
        plugin_enabled_ = false;
    }

    bool PxPluginInterface::IsWorking() {
        return plugin_enabled_;
    }

    bool PxPluginInterface::OnCreate(const PxPluginParam& param) {
        if (lifecycle_state_.load() == PxPluginLifecycleState::Running) {
            return true;
        }
        if (lifecycle_state_.load() == PxPluginLifecycleState::Destroyed) {
            return false;
        }
        SnowflakeId::initialize(0, 103);
        MemoryStat::Instance();
        this->param_ = param;
        if (param.cluster_.contains("name")) {
            auto n = param.cluster_.at("name");
            plugin_file_name_ = std::any_cast<std::string>(n);
        }

        if (param.cluster_.contains("base_path")) {
            base_path_ = std::any_cast<std::string>(param.cluster_.at("base_path"));
        }
        if (param.cluster_.contains("base_data_path")) {
            base_data_path_ = std::any_cast<std::wstring>(param.cluster_.at("base_data_path"));
        }
        plugin_context_ = std::make_shared<PxPluginContext>(GetPluginName());
        const auto log_path = std::format(L"{}/px_logs/{}.log", base_data_path_, StringUtil::ToWString(plugin_file_name_));
        Logger::InitLog(log_path, true);
        LOGI("{} OnCreate", GetPluginName());

        capture_audio_device_id_ = GetConfigParam<std::string>("capture_audio_device_id");
        sys_settings_.device_id_ = GetConfigParam<std::string>("device_id");
        if (param.cluster_.contains("direct_allow_takeover")) {
            sys_settings_.direct_allow_takeover_ =
                GetConfigBoolParam("direct_allow_takeover");
        }
        sys_settings_.relay_enabled_ = GetConfigBoolParam("relay_enabled");
        sys_settings_.language_ = (int)GetConfigIntParam("language");
        sys_settings_.appkey_ = GetConfigStringParam("appkey");

        // print params
        LOGI("Input params size : {}", param.cluster_.size());
        for (const auto& [key, value]: param.cluster_) {
            if (value.type() == typeid(std::string)) {
                const auto lower_key = StringUtil::ToLowerCpy(key);
                const bool sensitive = lower_key.find("appkey") != std::string::npos
                    || lower_key.find("ticket") != std::string::npos
                    || lower_key.find("token") != std::string::npos
                    || lower_key.find("nonce") != std::string::npos
                    || lower_key.find("password") != std::string::npos
                    || lower_key.find("pwd") != std::string::npos;
                LOGI(" * {} => {}", key,
                     sensitive ? "<redacted>" : std::any_cast<std::string>(value));
            }
            else if (value.type() == typeid(int64_t)) {
                LOGI(" * {} => {}", key, std::any_cast<int64_t>(value));
            }
            else if (value.type() == typeid(double)) {
                LOGI(" * {} => {}", key, std::any_cast<double>(value));
            }
            else if (value.type() == typeid(bool)) {
                LOGI(" * {} => {}", key, std::any_cast<bool>(value));
            }
        }

        stopped_ = false;
        destroyed_ = false;
        lifecycle_state_ = PxPluginLifecycleState::Running;
        return true;
    }

    bool PxPluginInterface::OnResume() {
        if (lifecycle_state_.load() == PxPluginLifecycleState::Destroyed) {
            return false;
        }
        this->stopped_ = false;
        lifecycle_state_ = PxPluginLifecycleState::Running;
        return true;
    }

    bool PxPluginInterface::OnStop() {
        if (lifecycle_state_.load() == PxPluginLifecycleState::Destroyed) {
            return true;
        }
        lifecycle_state_ = PxPluginLifecycleState::Stopping;
        this->stopped_ = true;
        event_channel_->Deactivate();
        event_cbk_ = nullptr;
        return true;
    }

    bool PxPluginInterface::OnDestroy() {
        if (lifecycle_state_.load() == PxPluginLifecycleState::Destroyed) {
            return true;
        }
        lifecycle_state_ = PxPluginLifecycleState::Stopping;
        stopped_ = true;
        event_channel_->Deactivate();
        event_cbk_ = nullptr;
        if (plugin_context_) {
            plugin_context_->OnDestroy();
            plugin_context_.reset();
        }
        destroyed_ = true;
        lifecycle_state_ = PxPluginLifecycleState::Destroyed;
        return true;
    }

    bool PxPluginInterface::IsStoppingOrDestroyed() const {
        auto state = lifecycle_state_.load();
        return stopped_ || destroyed_
            || state == PxPluginLifecycleState::Stopping
            || state == PxPluginLifecycleState::Destroyed;
    }

    void PxPluginInterface::PostWorkTask(std::function<void()>&& task) {
        if (plugin_context_ && !IsStoppingOrDestroyed()) {
            plugin_context_->PostWorkTask(std::move(task));
        }
    }

    void PxPluginInterface::PostUITask(std::function<void()>&& task) {
        if (IsStoppingOrDestroyed()) {
            return;
        }
        if (task) {
            task();
        }
    }

    void PxPluginInterface::PostUIDelayTask(int ms, std::function<void()>&& task) {
        if (plugin_context_ && !IsStoppingOrDestroyed()) {
            plugin_context_->PostDelayTask(std::move(task), ms);
        }
    }

    void PxPluginInterface::RegisterEventCallback(const PxPluginEventCallback& cbk) {
        event_cbk_ = cbk;
        event_channel_->Register(cbk);
    }

    void PxPluginInterface::CallbackEvent(const std::shared_ptr<PxPluginBaseEvent>& event) {
        if (!event || IsStoppingOrDestroyed() || !event_channel_->CanDeliver()) {
            return;
        }
        event->plugin_name_ = GetPluginId();
        const auto weak_channel = std::weak_ptr<PxPluginEventChannel>(event_channel_);
        PostWorkTask([weak_channel, event]() {
            if (const auto channel = weak_channel.lock()) {
                channel->Deliver(event);
            }
        });
    }

    void PxPluginInterface::CallbackEventDirectly(const std::shared_ptr<PxPluginBaseEvent>& event) {
        if (event && !IsStoppingOrDestroyed()) {
            event_channel_->Deliver(event);
        }
    }

    PxPluginEventCallback PxPluginInterface::MakeDirectEventDispatcher() const {
        const auto weak_channel =
            std::weak_ptr<PxPluginEventChannel>(event_channel_);
        return [weak_channel](const std::shared_ptr<PxPluginBaseEvent>& event) {
            if (event) {
                if (const auto channel = weak_channel.lock()) {
                    channel->Deliver(event);
                }
            }
        };
    }

    void PxPluginInterface::On1Second() {

    }

    void PxPluginInterface::InsertIdr() {
        auto event = std::make_shared<PxPluginInsertIdrEvent>();
        CallbackEvent(event);
    }

    void PxPluginInterface::OnCommand(const std::string& command) {

    }

    void PxPluginInterface::OnNewClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& conn_type) {
        no_connected_clients_counter_ = 0;
    }

    void PxPluginInterface::OnClientDisconnected(const std::string& visitor_device_id, const std::string& stream_id) {

    }

    void PxPluginInterface::OnMessage(std::shared_ptr<Message> msg) {

    }

    void PxPluginInterface::OnSyncPluginSettingsInfo(const px::PxPluginSettingsInfo& settings) {
        if (!settings.device_id_.empty()) {
            sys_settings_.device_id_ = settings.device_id_;
        }
        if (!settings.device_random_pwd_.empty()) {
            sys_settings_.device_random_pwd_ = settings.device_random_pwd_;
        }
        if (!settings.device_safety_pwd_.empty()) {
            sys_settings_.device_safety_pwd_ = settings.device_safety_pwd_;
        }
        if (!settings.relay_host_.empty()) {
            sys_settings_.relay_host_ = settings.relay_host_;
        }
        if (!settings.relay_port_.empty()) {
            sys_settings_.relay_port_ = settings.relay_port_;
        }
        sys_settings_.can_be_operated_ = settings.can_be_operated_;
        sys_settings_.direct_allow_takeover_ = settings.direct_allow_takeover_;
        sys_settings_.relay_enabled_ = settings.relay_enabled_;
        sys_settings_.language_ = settings.language_;
        sys_settings_.file_transfer_enabled_ = settings.file_transfer_enabled_;
        sys_settings_.audio_enabled_ = settings.audio_enabled_;
        sys_settings_.appkey_ = settings.appkey_;
        sys_settings_.role_ = settings.role_;
        //LOGI("OnSyncSettings: device id: {}, random pwd: {}, safety pwd: {}, relay host: {}, port: {}, relay enabled: {}, language: {}, role: {}",
        //     sys_settings_.device_id_, sys_settings_.device_random_pwd_, sys_settings_.device_safety_pwd_, sys_settings_.relay_host_,
        //     sys_settings_.relay_port_, sys_settings_.relay_enabled_, sys_settings_.language_, sys_settings_.role_);
    }

    PxPluginSettingsInfo PxPluginInterface::GetPluginSettingsInfo() {
        return sys_settings_;
    }

    void PxPluginInterface::DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
        if (event->type_ == AppBaseEvent::EType::kConnectedClientCount) {
            auto target_evt = std::dynamic_pointer_cast<MsgConnectedClientCount>(event);
            //LOGI("connected clients count: {}", target_evt->connected_client_count_);
            if (target_evt->connected_client_count_ <= 0) {
                no_connected_clients_counter_++;
            }
            else {
                no_connected_clients_counter_ = 0;
            }
        }
    }

    bool PxPluginInterface::DontHaveConnectedClientsNow() {
        auto dont_have = no_connected_clients_counter_ > 10;
        //LOGI("dont have: {}, count: {}", dont_have, no_connected_clients_counter_);
        return dont_have;
    }

    void PxPluginInterface::UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& msg) {

    }

}
