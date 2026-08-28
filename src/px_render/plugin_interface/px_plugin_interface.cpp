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
        event_cbk_ = nullptr;
        return true;
    }

    bool PxPluginInterface::OnDestroy() {
        if (lifecycle_state_.load() == PxPluginLifecycleState::Destroyed) {
            return true;
        }
        lifecycle_state_ = PxPluginLifecycleState::Stopping;
        stopped_ = true;
        event_cbk_ = nullptr;
        if (plugin_context_) {
            plugin_context_->OnDestroy();
            plugin_context_.reset();
        }
        net_plugins_.clear();
        total_plugins_.clear();
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
    }

    void PxPluginInterface::CallbackEvent(const std::shared_ptr<PxPluginBaseEvent>& event) {
        if (!event_cbk_ || IsStoppingOrDestroyed()) {
            return;
        }
        event->plugin_name_ = GetPluginId();
        PostWorkTask([=, this]() {
            if (event_cbk_ && !IsStoppingOrDestroyed()) {
                event_cbk_(event);
            }
        });
    }

    void PxPluginInterface::CallbackEventDirectly(const std::shared_ptr<PxPluginBaseEvent>& event) {
        if (event_cbk_ && !IsStoppingOrDestroyed()) {
            event_cbk_(event);
        }
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

    void PxPluginInterface::AttachNetPlugin(const std::string& id, PxNetPlugin* plugin) {
        net_plugins_[id] = plugin;
    }

    void PxPluginInterface::AttachPlugin(const std::string& id, PxPluginInterface* plugin) {
        total_plugins_[id] = plugin;
    }

    bool PxPluginInterface::HasAttachedNetPlugins() {
        return !net_plugins_.empty();
    }

    // 单插件投递耗时超过该阈值时告警:net 插件串行分发,任一插件在分发线程上
    // 阻塞都会拖垮整条媒体管线(曾因此导致 render 整体假死)。告警限频,每 10s 一条。
    static constexpr int64_t kSlowPluginDispatchThresholdMs = 200;
    static std::atomic<int64_t> last_slow_dispatch_log_ts = 0;

    static void LogSlowPluginDispatch(const std::string& plugin_id, const char* api, int64_t cost_ms) {
        auto now = (int64_t)px::TimeUtil::GetCurrentTimestamp();
        auto last = last_slow_dispatch_log_ts.load();
        if (now - last >= 10000 && last_slow_dispatch_log_ts.compare_exchange_strong(last, now)) {
            LOGW("Slow net plugin dispatch: {} cost {}ms in {}", plugin_id, cost_ms, api);
        }
    }

    // FT 派发失败(该 net 插件不承载此 stream,或对端已断开)本是常态:
    // 分发循环遍历所有 net 插件,不承载的必然返回 false;对端断开后在插件级
    // 断线事件清理作业前,引擎还会继续往死通道派几个消息。逐条刷 warn 会淹掉
    // 日志(实机观察过数十秒刷屏),按 插件+stream 限频,每 10s 一条。
    static std::mutex ft_dispatch_fail_log_mtx;
    static std::map<std::string, int64_t> ft_dispatch_fail_log_ts;

    static void LogFtDispatchFailed(const std::string& plugin_id, const std::string& stream_id) {
        auto now = (int64_t)px::TimeUtil::GetCurrentTimestamp();
        std::lock_guard<std::mutex> lk(ft_dispatch_fail_log_mtx);
        if (ft_dispatch_fail_log_ts.size() > 1024) {
            ft_dispatch_fail_log_ts.clear(); // 防长跑进程慢涨,清了重来即可
        }
        auto& last = ft_dispatch_fail_log_ts[plugin_id + "|" + stream_id];
        if (now - last >= 10000) {
            last = now;
            LOGW("DispatchTargetFileTransferMessage failed in plugin: {}, stream: {} (rate limited, 1/10s)",
                 plugin_id, stream_id);
        }
    }

    void PxPluginInterface::DispatchAllStreamMessage(std::shared_ptr<Data> msg, bool run_through) {
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            auto begin = px::TimeUtil::GetCurrentTimestamp();
            plugin->PostProtoMessage(msg, run_through);
            auto cost = (int64_t)px::TimeUtil::GetCurrentTimestamp() - (int64_t)begin;
            if (cost > kSlowPluginDispatchThresholdMs) {
                LogSlowPluginDispatch(plugin_id, "PostProtoMessage", cost);
            }
        }
    }

    void PxPluginInterface::DispatchTargetStreamMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            auto begin = px::TimeUtil::GetCurrentTimestamp();
            plugin->PostTargetStreamProtoMessage(stream_id, msg, run_through);
            auto cost = (int64_t)px::TimeUtil::GetCurrentTimestamp() - (int64_t)begin;
            if (cost > kSlowPluginDispatchThresholdMs) {
                LogSlowPluginDispatch(plugin_id, "PostTargetStreamProtoMessage", cost);
            }
        }
    }

    void PxPluginInterface::DispatchTargetFileTransferMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            auto begin = px::TimeUtil::GetCurrentTimestamp();
            const bool accepted = plugin->PostTargetFileTransferProtoMessage(stream_id, msg, run_through);
            auto cost = (int64_t)px::TimeUtil::GetCurrentTimestamp() - (int64_t)begin;
            if (cost > kSlowPluginDispatchThresholdMs) {
                LogSlowPluginDispatch(plugin_id, "PostTargetFileTransferProtoMessage", cost);
            }
            if (accepted) {
                return;
            }
            LogFtDispatchFailed(plugin_id, stream_id);
        }
    }

    FileTransferSendResult PxPluginInterface::DispatchTargetFileTransferMessageOnRoute(
        const std::string& plugin_id,
        const std::string& stream_id,
        std::shared_ptr<Data> msg,
        bool run_through,
        const std::string& connection_instance_id) {
        if (!msg) {
            return FileTransferSendResult::TransportError("file-transfer payload is empty");
        }
        const auto route = net_plugins_.find(plugin_id);
        if (route == net_plugins_.end()) {
            return FileTransferSendResult::Disconnected("file-transfer route is unavailable");
        }
        if (route->second->GetConnectedClientsCount() <= 0) {
            return FileTransferSendResult::Disconnected("file-transfer route has no connected client");
        }
        const auto begin = px::TimeUtil::GetCurrentTimestamp();
        const bool accepted = route->second->PostTargetFileTransferProtoMessage(
            stream_id, std::move(msg), run_through, connection_instance_id);
        const auto cost = static_cast<int64_t>(px::TimeUtil::GetCurrentTimestamp()) -
                          static_cast<int64_t>(begin);
        if (cost > kSlowPluginDispatchThresholdMs) {
            LogSlowPluginDispatch(plugin_id, "PostTargetFileTransferProtoMessage", cost);
        }
        if (!accepted) {
            LogFtDispatchFailed(plugin_id, stream_id);
            return FileTransferSendResult::Busy(
                "selected file-transfer route is not ready or congested");
        }
        return FileTransferSendResult::Accepted();
    }

    void PxPluginInterface::OnMessage(std::shared_ptr<Message> msg) {

    }

    void PxPluginInterface::OnMessageRaw(const std::any& msg) {

    }

    std::map<std::string, PxNetPlugin*> PxPluginInterface::GetNetPlugins() {
        return net_plugins_;
    }

    int64_t PxPluginInterface::GetQueuingMediaMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingMediaMsgCount();
                //LOGI("Queuing msg count in [{}] is : {}", plugin_id, plugin->GetQueuingMediaMsgCount());
            }
        }
        return queuing_msg_count;
    }

    int64_t PxPluginInterface::GetQueuingFtMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingFtMsgCount();
            }
        }
        return queuing_msg_count;
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

    PxPluginInterface* PxPluginInterface::GetPluginById(const std::string& plugin_id) {
        for (const auto& [id, plugin] : total_plugins_) {
            if (plugin_id == plugin->GetPluginId()) {
                return plugin;
            }
        }
        return nullptr;
    }

}
