//
// Created by RGAA on 15/11/2024.
//

#include "plugin_manager.h"
#include <filesystem>
#include <cctype>
#include <mutex>
#include "rd_app.h"
#include "plugin_ids.h"
#include "rd_context.h"
#include "px_common_new/log.h"
#include "px_common_new/win32/win_helper.h"
#include "px_common_new/string_util.h"
#include "plugin_event_router.h"
#include "settings/rd_settings.h"
#include "px_common_new/folder_util.h"
#include "px_render/plugin_interface/px_net_plugin.h"
#include "px_render/plugin_interface/px_stream_plugin.h"
#include "px_render/plugin_interface/px_plugin_interface.h"
#include "px_render/plugin_interface/px_video_encoder_plugin.h"
#include "px_render/plugin_interface/px_monitor_capture_plugin.h"

typedef void *(*FnGetInstance)();

namespace px
{

    std::shared_ptr<PluginManager> PluginManager::Make(const std::shared_ptr<RdApplication>& app) {
        return std::make_shared<PluginManager>(app);
    }

    PluginManager::PluginManager(const std::shared_ptr<RdApplication>& app) {
        this->app_ = app;
        this->context_ = app->GetContext();
        settings_ = RdSettings::Instance();
    }

    PluginManager::~PluginManager() {
        exiting_ = true;
    }

    void PluginManager::LoadAllPlugins() {
        exiting_ = false;
        auto base_path = WinHelper::GetExeFolderPath();
        auto base_data_path = FolderUtil::GetProgramDataPath();
        LOGI("plugin base path: {}", base_path);
        LOGI("plugin base data path: {}", StringUtil::ToUTF8(base_data_path));

        auto plugin_dir = PathFromUTF8(base_path) / "deps" / "rd_plugins";
        if (!std::filesystem::exists(plugin_dir)) {
            LOGW("Plugin directory does not exist: {}", plugin_dir.string());
            return;
        }

        std::string ext = ".dll";
#if !WIN32
        ext = ".so";
#endif

        for (const auto& entry : std::filesystem::directory_iterator(plugin_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto ext_name = entry.path().extension().string();
            if (ext_name.length() != ext.length()) {
                continue;
            }
            bool match = true;
            for (size_t i = 0; i < ext.length(); ++i) {
                if (std::tolower(ext_name[i]) != std::tolower(ext[i])) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }

            auto target_plugin_path = entry.path().wstring();
            auto plugin_id = entry.path().stem().string();
            LOGI("Will load: {}", StringUtil::ToUTF8(target_plugin_path));

            auto library = std::make_shared<DynamicLibrary>(target_plugin_path);
            if (library->IsLoaded()) {
                LOGW("This plugin: {} was already loaded.", StringUtil::ToUTF8(target_plugin_path));
                continue;
            }
            if (!library->Load()) {
                LOGE("Load plugin failed, plugin_id: {}, dll path: {}, errorString: {}",
                     plugin_id,
                     StringUtil::ToUTF8(target_plugin_path),
                     library->GetErrorString());
                continue;
            }
            auto fn_get_instance = (FnGetInstance)library->GetSymbol("GetInstance");

            auto func = (FnGetInstance) fn_get_instance;
            if (func) {
                auto plugin = (PxPluginInterface*)func();
                if (plugin) {
                    plugin_id = plugin->GetPluginId();
                    if (plugins_.contains(plugin_id)) {
                        LOGE("Load plugin failed, plugin_id: {}, dll path: {}, errorString: repeated loading",
                             plugin_id,
                             StringUtil::ToUTF8(target_plugin_path));
                        continue;
                    }

                    auto filename = entry.path().filename().string();
                    auto param = PxPluginParam {
                        .cluster_ = {
                            {"name", filename},
                            {"base_path", base_path},
                            {"base_data_path", base_data_path},
                            // Always empty: MiniAudio loopback follows the OS default device.
                            {"capture_audio_device_id", std::string("")},
                            {"ws-listen-port", (int64_t)settings_->transmission_.listening_port_},
                            // TCP/ws 与 UDP 媒体面共用同一端口(见 udp_gamestream_channel_state.md)
                            {"udp-listen-port", (int64_t)settings_->transmission_.listening_port_},
                            {"device_id", settings_->device_id_},
                            {"relay_enabled", settings_->relay_enabled_},
                            {"relay_host", settings_->relay_host_},
                            {"relay_port", settings_->relay_port_},
                            {"language", (int64_t)settings_->language_},
                            {"appkey", settings_->appkey_},
                            // 插件 DLL 内的 RdSettings::Instance() 是独立副本(header 内 static),
                            // 模式必须由 exe 侧显式下发
                            {"app_mode", std::string(settings_->IsGameHookMode() ? "game-hook" : "desktop")}
                        },
                    };

                    if (!plugin->OnCreate(param)) {
                        LOGE("Load plugin failed, plugin_id: {}, dll path: {}, errorString: OnCreate failed for {}",
                             plugin_id,
                             StringUtil::ToUTF8(target_plugin_path),
                             plugin->GetPluginName());
                        continue;
                    }

                    if (!plugin->IsPluginEnabled()) {
                        LOGW("Plugin: {} is disabled!", plugin->GetPluginName());
                    }

                    plugins_.insert({plugin_id, plugin});
                    plugin_libraries_.insert({plugin_id, library});

                    LOGI("{} loaded, version: {}", plugin->GetPluginName(), plugin->GetVersionName());
                } else {
                    LOGE("Load plugin failed, plugin_id: {}, dll path: {}, errorString: object create failed",
                         plugin_id,
                         StringUtil::ToUTF8(target_plugin_path));
                }
            } else {
                LOGE("Load plugin failed, plugin_id: {}, dll path: {}, errorString: cannot resolve GetInstance: {}",
                     plugin_id,
                     StringUtil::ToUTF8(target_plugin_path),
                     library->GetErrorString());
            }
        }

        // net plugins
        std::vector<PxPluginInterface*> plugins;
        VisitAllPlugins([&](PxPluginInterface* plugin) {
            plugins.push_back(plugin);
            if (plugin->GetPluginType() != PxPluginType::kNet) {
                VisitNetPlugins([=, this](PxNetPlugin* np) {
                    plugin->AttachNetPlugin(np->GetPluginId(), np);
                });
            }
        });

        // total plugins
        VisitAllPlugins([&](PxPluginInterface* plugin) {
            for (PxPluginInterface* p : plugins) {
                if (p->GetPluginId() == plugin->GetPluginId()) {
                    continue;
                }
                plugin->AttachPlugin(p->GetPluginId(), p);
            }
        });
    }

    void PluginManager::RegisterPluginEventsCallback() {
        this->evt_router_ = std::make_shared<PluginEventRouter>(app_);
        auto weak_self = weak_from_this();
        VisitAllPlugins([&](PxPluginInterface* plugin) {
            plugin->RegisterEventCallback([weak_self](const std::shared_ptr<PxPluginBaseEvent>& event) {
                auto self = weak_self.lock();
                if (!self || self->exiting_ || !self->evt_router_) {
                    return;
                }
                self->evt_router_->ProcessPluginEvent(event);
            });
        });
    }

    void PluginManager::ReleaseAllPlugins() {
        // reject new visitors first, the event callback registered in
        // RegisterPluginEventsCallback also checks this flag before routing
        exiting_ = true;
        // wait until in-flight visitors leave, then detach the plugin map;
        // OnStop/OnDestroy run outside the lock because plugins may fire
        // events which re-enter the visiting functions
        std::map<std::string, PxPluginInterface*> plugins;
        {
            std::unique_lock<std::shared_mutex> lock(plugins_mtx_);
            plugins.swap(plugins_);
        }
        for (const auto& [k, plugin] : plugins) {
            plugin->OnStop();
        }
        for (const auto& [k, plugin] : plugins) {
            plugin->OnDestroy();
        }
        plugin_libraries_.clear();
        evt_router_.reset();
    }

    void PluginManager::ReleasePlugin(const std::string &name) {

    }

    PxPluginInterface* PluginManager::GetPluginById(const std::string& id) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        if (!plugins_.contains(id)) {
            return nullptr;
        }
        return plugins_.at(id);
    }

    PxVideoEncoderPlugin* PluginManager::GetFFmpegEncoderPlugin() {
        auto plugin = GetPluginById(kFFmpegEncoderPluginId);
        if (plugin) {
            return (PxVideoEncoderPlugin*)plugin;
        }
        return nullptr;
    }

    PxVideoEncoderPlugin* PluginManager::GetNvencEncoderPlugin() {
        auto plugin = GetPluginById(kNvencEncoderPluginId);
        if (plugin) {
            return (PxVideoEncoderPlugin*)plugin;
        }
        return nullptr;
    }

    PxVideoEncoderPlugin* PluginManager::GetAmfEncoderPlugin() {
        auto plugin = GetPluginById(kAmfEncoderPluginId);
        if (plugin) {
            return (PxVideoEncoderPlugin*)plugin;
        }
        return nullptr;
    }

    PxMonitorCapturePlugin* PluginManager::GetDDACapturePlugin() {
        auto plugin = GetPluginById(kDdaCapturePluginId);
        if (plugin) {
            return (PxMonitorCapturePlugin*)plugin;
        }
        return nullptr;
    }

    PxMonitorCapturePlugin* PluginManager::GetGdiCapturePlugin() {
        auto plugin = GetPluginById(kGdiCapturePluginId);
        if (plugin) {
            return (PxMonitorCapturePlugin*)plugin;
        }
        return nullptr;
    }

    PxDataProviderPlugin* PluginManager::GetMockVideoStreamPlugin() {
        auto plugin = GetPluginById(kMockVideoStreamPluginId);
        if (plugin) {
            return (PxDataProviderPlugin*)plugin;
        }
        return nullptr;
    }

    PxDataProviderPlugin* PluginManager::GetAudioCapturePlugin() {
        auto plugin = GetPluginById(kWasAudioCapturePluginId);
        if (plugin) {
            return (PxDataProviderPlugin*)plugin;
        }
        return nullptr;
    }

    PxAudioEncoderPlugin* PluginManager::GetAudioEncoderPlugin() {
        auto plugin = GetPluginById(kOpusEncoderPluginId);
        if (plugin) {
            return (PxAudioEncoderPlugin*)plugin;
        }
        return nullptr;
    }

    PxNetPlugin* PluginManager::GetUdpPlugin() {
        auto plugin = GetPluginById(kNetUdpPluginId);
        if (plugin) {
            return (PxNetPlugin*)plugin;
        }
        return nullptr;
    }

    PxPluginInterface* PluginManager::GetClipboardPlugin() {
        auto plugin = GetPluginById(kClipboardPluginId);
        if (plugin) {
            return (PxPluginInterface*)plugin;
        }
        return nullptr;
    }

    PxPluginInterface* PluginManager::GetRtcPlugin() {
        auto plugin = GetPluginById(kNetRtcPluginId);
        if (plugin) {
            return plugin;
        }
        return nullptr;
    }

    PxNetPlugin* PluginManager::GetRelayPlugin() {
        auto plugin = GetPluginById(kRelayPluginId);
        if (plugin) {
            return (PxNetPlugin*)plugin;
        }
        return nullptr;
    }

    PxFrameCarrierPlugin* PluginManager::GetFrameCarrierPlugin() {
        auto plugin = GetPluginById(kFrameCarrierPluginId);
        if (plugin) {
            return (PxFrameCarrierPlugin*)plugin;
        }
        return nullptr;
    }

    PxFrameProcessorPlugin* PluginManager::GetFrameResizePlugin() {
        auto plugin = GetPluginById(kFrameResizerPluginId);
        if (plugin) {
            return (PxFrameProcessorPlugin*)plugin;
        }
        return nullptr;
    }

    PxPluginInterface* PluginManager::GetEventsReplayerPlugin() {
        auto plugin = GetPluginById(kEventReplayerPluginId);
        if (plugin) {
            return plugin;
        }
        return nullptr;
    }

    PxPluginInterface* PluginManager::GetRtcLocalPlugin() {
        auto plugin = GetPluginById(kNetRtcLocalPluginId);
        if (plugin) {
            return plugin;
        }
        return nullptr;
    }

    PxPluginInterface* PluginManager::GetFtPlugin() {
        auto plugin = GetPluginById(kFtPluginId);
        if (plugin) {
            return plugin;
        }
        return nullptr;
    }

    void PluginManager::VisitAllPlugins(const std::function<void(PxPluginInterface *)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (visitor) {
                visitor(plugin);
            }
        }
    }

    void PluginManager::VisitStreamPlugins(const std::function<void(PxStreamPlugin *)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (plugin->GetPluginType() == PxPluginType::kStream) {
                visitor((PxStreamPlugin *) plugin);
            }
        }
    }

    void PluginManager::VisitUtilPlugins(const std::function<void(PxPluginInterface *)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (plugin->GetPluginType() == PxPluginType::kUtil) {
                visitor(plugin);
            }
        }
    }

    void PluginManager::VisitEncoderPlugins(const std::function<void(PxVideoEncoderPlugin*)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (plugin->GetPluginType() == PxPluginType::kEncoder) {
                visitor((PxVideoEncoderPlugin *) plugin);
            }
        }
    }

    void PluginManager::VisitNetPlugins(const std::function<void(PxNetPlugin*)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (plugin->GetPluginType() == PxPluginType::kNet) {
                visitor((PxNetPlugin*) plugin);
            }
        }
    }

    void PluginManager::On1Second() {
        if (exiting_ || !context_) {
            return;
        }
        auto context = context_;
        auto weak_self = weak_from_this();
        context->PostTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->context_ || !self->evt_router_) {
                return;
            }

            // hold the shared lock for the whole visit, so ReleaseAllPlugins
            // cannot destroy the plugins while they are in use here
            std::shared_lock<std::shared_mutex> lock(self->plugins_mtx_);
            if (self->plugins_.empty()) {
                return;
            }

            std::vector<PxPluginInterface*> plugins_snapshot;
            plugins_snapshot.reserve(self->plugins_.size());
            for (const auto& [k, plugin] : self->plugins_) {
                if (plugin) {
                    plugins_snapshot.push_back(plugin);
                }
            }

            int connected_client_count = 0;
            for (auto* plugin : plugins_snapshot) {
                if (self->exiting_) {
                    return;
                }
                if (plugin->GetPluginType() == PxPluginType::kNet) {
                    connected_client_count += ((PxNetPlugin*)plugin)->GetConnectedClientsCount();
                }
            }

            //LOGI("connected_client_count: {}", connected_client_count);
            for (auto* plugin : plugins_snapshot) {
                if (self->exiting_) {
                    return;
                }
                plugin->On1Second();

                // connected clients count
                {
                    auto event = std::make_shared<MsgConnectedClientCount>();
                    event->connected_client_count_ = connected_client_count;
                    plugin->DispatchAppEvent(event);
                }
            }
        });
    }

    void PluginManager::DumpPluginInfo() {
        LOGI("====> Total plugins: {}", plugins_.size());
        int index = 1;
        VisitAllPlugins([&](PxPluginInterface *plugin) {
            LOGI("Plugin {}. [{}] vn: [{}], vc: [{}], enabled: [{}]",
                 index++,
                 plugin->GetPluginName(),
                 plugin->GetVersionName(),
                 plugin->GetVersionCode(),
                 plugin->IsPluginEnabled()
            );
        });
    }

    void PluginManager::SyncPluginSettingsInfo(const PxPluginSettingsInfo& info) {
        if (exiting_) {
            return;
        }
        VisitAllPlugins([&](PxPluginInterface* plugin) {
            plugin->OnSyncPluginSettingsInfo(info);
        });
    }

    int64_t PluginManager::GetQueuingMediaMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        VisitNetPlugins([&](PxNetPlugin* plugin) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingMediaMsgCount();
            }
        });
        return queuing_msg_count;
    }

    int64_t PluginManager::GetQueuingFtMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        VisitNetPlugins([&](PxNetPlugin* plugin) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingFtMsgCount();
            }
        });
        return queuing_msg_count;
    }

    int PluginManager::GetTotalConnectedClientsCount() {
        int total_size = 0;
        VisitNetPlugins([&](PxNetPlugin* plugin) {
            total_size += plugin->GetConnectedClientsCount();
        });
        return total_size;
    }

    std::vector<std::shared_ptr<PxConnectedClientInfo>> PluginManager::GetConnectedClientsInfo() {
        std::vector<std::shared_ptr<PxConnectedClientInfo>> clients_info;
        VisitNetPlugins([&](PxNetPlugin* plugin) {
            if (auto cs = plugin->GetConnectedClientInfo(); !cs.empty()) {
                for (auto& info : cs) {
                    clients_info.push_back(info);
                }
            }
        });
        return clients_info;
    }

    // is GDI
    bool PluginManager::IsGDIMonitorCapturePlugin(PxMonitorCapturePlugin* plugin) {
        return plugin && plugin->GetPluginId() == kGdiCapturePluginId;
    }

    // is DDA
    bool PluginManager::IsDDAMonitorCapturePlugin(PxMonitorCapturePlugin* plugin) {
        return plugin && plugin->GetPluginId() == kDdaCapturePluginId;
    }

}
