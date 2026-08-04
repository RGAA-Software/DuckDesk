//
// Created by RGAA on 15/11/2024.
//

#include "gr_plugin_interface.h"
#include "tc_common_new/image.h"
#include "tc_common_new/data.h"
#include "tc_common_new/thread.h"
#include "gr_plugin_events.h"
#include "tc_common_new/log.h"
#include "tc_common_new/memory_stat.h"
#include "gr_plugin_context.h"
#include "tc_common_new/snowflake_id.h"
#include <tc_common_new/string_util.h>
#include "tc_common_new/time_util.h"
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

namespace tc
{

    std::shared_ptr<GrPluginContext> GrPluginInterface::GetPluginContext() {
        return plugin_context_;
    }

    std::string GrPluginInterface::GetPluginName() {
        return "dummy";
    }

    std::string GrPluginInterface::GetPluginAuthor() {
        return plugin_author_;
    }

    std::string GrPluginInterface::GetPluginDescription() {
        return "plugin description";
    }

    GrPluginType GrPluginInterface::GetPluginType() {
        return plugin_type_;
    }

    bool GrPluginInterface::IsStreamPlugin() {
        return plugin_type_ == GrPluginType::kStream;
    }

    std::string GrPluginInterface::GetVersionName() {
        return "1.0.0";
    }

    uint32_t GrPluginInterface::GetVersionCode() {
        return 1;
    }

    bool GrPluginInterface::IsPluginEnabled() {
        return plugin_enabled_;
    }

    void GrPluginInterface::EnablePlugin() {
        plugin_enabled_ = true;
    }

    void GrPluginInterface::DisablePlugin() {
        plugin_enabled_ = false;
    }

    bool GrPluginInterface::IsWorking() {
        return plugin_enabled_;
    }

    bool GrPluginInterface::OnCreate(const GrPluginParam& param) {
        if (lifecycle_state_.load() == GrPluginLifecycleState::Running) {
            return true;
        }
        if (lifecycle_state_.load() == GrPluginLifecycleState::Destroyed) {
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
        plugin_context_ = std::make_shared<GrPluginContext>(GetPluginName());
        const auto log_path = std::format(L"{}/gr_logs/{}.log", base_data_path_, StringUtil::ToWString(plugin_file_name_));
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
                LOGI(" * {} => {}", key, std::any_cast<std::string>(value));
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

            // parse
            if (key == "author") {
                plugin_author_ = std::any_cast<std::string>(value);
            }
            else if (key == "description") {
                plugin_desc_ = std::any_cast<std::string>(value);
            }
            else if (key == "version_name") {
                plugin_version_name_ = std::any_cast<std::string>(value);
            }
            else if (key == "version_code") {
                plugin_version_code_ = std::any_cast<int64_t>(value);
            }
            else if (key == "enabled") {
                plugin_enabled_ = std::any_cast<bool>(value);
            }
        }

        stopped_ = false;
        destroyed_ = false;
        lifecycle_state_ = GrPluginLifecycleState::Running;
        return true;
    }

    bool GrPluginInterface::OnResume() {
        if (lifecycle_state_.load() == GrPluginLifecycleState::Destroyed) {
            return false;
        }
        this->stopped_ = false;
        lifecycle_state_ = GrPluginLifecycleState::Running;
        return true;
    }

    bool GrPluginInterface::OnStop() {
        if (lifecycle_state_.load() == GrPluginLifecycleState::Destroyed) {
            return true;
        }
        lifecycle_state_ = GrPluginLifecycleState::Stopping;
        this->stopped_ = true;
        event_cbk_ = nullptr;
        return true;
    }

    bool GrPluginInterface::OnDestroy() {
        if (lifecycle_state_.load() == GrPluginLifecycleState::Destroyed) {
            return true;
        }
        lifecycle_state_ = GrPluginLifecycleState::Stopping;
        stopped_ = true;
        event_cbk_ = nullptr;
        if (plugin_context_) {
            plugin_context_->OnDestroy();
            plugin_context_.reset();
        }
        net_plugins_.clear();
        total_plugins_.clear();
        destroyed_ = true;
        lifecycle_state_ = GrPluginLifecycleState::Destroyed;
        return true;
    }

    bool GrPluginInterface::IsStoppingOrDestroyed() const {
        auto state = lifecycle_state_.load();
        return stopped_ || destroyed_
            || state == GrPluginLifecycleState::Stopping
            || state == GrPluginLifecycleState::Destroyed;
    }

    void GrPluginInterface::PostWorkTask(std::function<void()>&& task) {
        if (plugin_context_ && !IsStoppingOrDestroyed()) {
            plugin_context_->PostWorkTask(std::move(task));
        }
    }

    void GrPluginInterface::PostUITask(std::function<void()>&& task) {
        if (IsStoppingOrDestroyed()) {
            return;
        }
        if (task) {
            task();
        }
    }

    void GrPluginInterface::PostUIDelayTask(int ms, std::function<void()>&& task) {
        if (plugin_context_ && !IsStoppingOrDestroyed()) {
            plugin_context_->PostDelayTask(std::move(task), ms);
        }
    }

    void GrPluginInterface::RegisterEventCallback(const GrPluginEventCallback& cbk) {
        event_cbk_ = cbk;
    }

    void GrPluginInterface::CallbackEvent(const std::shared_ptr<GrPluginBaseEvent>& event) {
        if (!event_cbk_ || IsStoppingOrDestroyed()) {
            return;
        }
        PostWorkTask([=, this]() {
            if (event_cbk_ && !IsStoppingOrDestroyed()) {
                event_cbk_(event);
            }
        });
    }

    void GrPluginInterface::CallbackEventDirectly(const std::shared_ptr<GrPluginBaseEvent>& event) {
        if (event_cbk_ && !IsStoppingOrDestroyed()) {
            event_cbk_(event);
        }
    }

    void GrPluginInterface::On1Second() {

    }

    void GrPluginInterface::InsertIdr() {
        auto event = std::make_shared<GrPluginInsertIdrEvent>();
        CallbackEvent(event);
    }

    void GrPluginInterface::OnCommand(const std::string& command) {

    }

    void GrPluginInterface::OnNewClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& conn_type) {
        no_connected_clients_counter_ = 0;
    }

    void GrPluginInterface::OnClientDisconnected(const std::string& visitor_device_id, const std::string& stream_id) {

    }

    void GrPluginInterface::AttachNetPlugin(const std::string& id, GrNetPlugin* plugin) {
        net_plugins_[id] = plugin;
    }

    void GrPluginInterface::AttachPlugin(const std::string& id, GrPluginInterface* plugin) {
        total_plugins_[id] = plugin;
    }

    bool GrPluginInterface::HasAttachedNetPlugins() {
        return !net_plugins_.empty();
    }

    // 单插件投递耗时超过该阈值时告警:net 插件串行分发,任一插件在分发线程上
    // 阻塞都会拖垮整条媒体管线(曾因此导致 render 整体假死)。告警限频,每 10s 一条。
    static constexpr int64_t kSlowPluginDispatchThresholdMs = 200;
    static std::atomic<int64_t> last_slow_dispatch_log_ts = 0;

    static void LogSlowPluginDispatch(const std::string& plugin_id, const char* api, int64_t cost_ms) {
        auto now = (int64_t)tc::TimeUtil::GetCurrentTimestamp();
        auto last = last_slow_dispatch_log_ts.load();
        if (now - last >= 10000 && last_slow_dispatch_log_ts.compare_exchange_strong(last, now)) {
            LOGW("Slow net plugin dispatch: {} cost {}ms in {}", plugin_id, cost_ms, api);
        }
    }

    void GrPluginInterface::DispatchAllStreamMessage(std::shared_ptr<Data> msg, bool run_through) {
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            auto begin = tc::TimeUtil::GetCurrentTimestamp();
            plugin->PostProtoMessage(msg, run_through);
            auto cost = (int64_t)tc::TimeUtil::GetCurrentTimestamp() - (int64_t)begin;
            if (cost > kSlowPluginDispatchThresholdMs) {
                LogSlowPluginDispatch(plugin_id, "PostProtoMessage", cost);
            }
        }
    }

    void GrPluginInterface::DispatchTargetStreamMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            auto begin = tc::TimeUtil::GetCurrentTimestamp();
            plugin->PostTargetStreamProtoMessage(stream_id, msg, run_through);
            auto cost = (int64_t)tc::TimeUtil::GetCurrentTimestamp() - (int64_t)begin;
            if (cost > kSlowPluginDispatchThresholdMs) {
                LogSlowPluginDispatch(plugin_id, "PostTargetStreamProtoMessage", cost);
            }
        }
    }

    void GrPluginInterface::DispatchTargetFileTransferMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            auto begin = tc::TimeUtil::GetCurrentTimestamp();
            if (!plugin->PostTargetFileTransferProtoMessage(stream_id, msg, run_through)) {
                LOGW("DispatchTargetFileTransferMessage failed in plugin: {}, stream: {}", plugin_id, stream_id);
            }
            auto cost = (int64_t)tc::TimeUtil::GetCurrentTimestamp() - (int64_t)begin;
            if (cost > kSlowPluginDispatchThresholdMs) {
                LogSlowPluginDispatch(plugin_id, "PostTargetFileTransferProtoMessage", cost);
            }
        }
    }

    void GrPluginInterface::OnMessage(std::shared_ptr<Message> msg) {

    }

    void GrPluginInterface::OnMessageRaw(const std::any& msg) {

    }

    std::map<std::string, GrNetPlugin*> GrPluginInterface::GetNetPlugins() {
        return net_plugins_;
    }

    int64_t GrPluginInterface::GetQueuingMediaMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingMediaMsgCount();
                //LOGI("Queuing msg count in [{}] is : {}", plugin_id, plugin->GetQueuingMediaMsgCount());
            }
        }
        return queuing_msg_count;
    }

    int64_t GrPluginInterface::GetQueuingFtMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        for (const auto& [plugin_id, plugin] : net_plugins_) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingFtMsgCount();
            }
        }
        return queuing_msg_count;
    }

    void GrPluginInterface::OnSyncPluginSettingsInfo(const tc::GrPluginSettingsInfo& settings) {
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

    GrPluginSettingsInfo GrPluginInterface::GetPluginSettingsInfo() {
        return sys_settings_;
    }

    void GrPluginInterface::DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
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

    bool GrPluginInterface::DontHaveConnectedClientsNow() {
        auto dont_have = no_connected_clients_counter_ > 10;
        //LOGI("dont have: {}, count: {}", dont_have, no_connected_clients_counter_);
        return dont_have;
    }

    void GrPluginInterface::UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& msg) {

    }

    GrPluginInterface* GrPluginInterface::GetPluginById(const std::string& plugin_id) {
        for (const auto& [id, plugin] : total_plugins_) {
            if (plugin_id == plugin->GetPluginId()) {
                return plugin;
            }
        }
        return nullptr;
    }

}
