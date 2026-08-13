//
// Created by RGAA on 15/11/2024.
//

#include "plugin_manager.h"
#include <filesystem>
#include <cctype>
#include <mutex>
#include <toml++/toml.hpp>
#include "rd_app.h"
#include "plugin_ids.h"
#include "rd_context.h"
#include "tc_common_new/log.h"
#include "tc_common_new/win32/win_helper.h"
#include "tc_common_new/string_util.h"
#include "plugin_event_router.h"
#include "settings/rd_settings.h"
#include "tc_common_new/folder_util.h"
#include "gr_render/plugin_interface/gr_net_plugin.h"
#include "gr_render/plugin_interface/gr_stream_plugin.h"
#include "gr_render/plugin_interface/gr_plugin_interface.h"
#include "gr_render/plugin_interface/gr_video_encoder_plugin.h"
#include "gr_render/plugin_interface/gr_monitor_capture_plugin.h"

typedef void *(*FnGetInstance)();

namespace tc
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

        auto plugin_dir = PathFromUTF8(base_path) / "gr_plugins";
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
                auto plugin = (GrPluginInterface*)func();
                if (plugin) {
                    plugin_id = plugin->GetPluginId();
                    if (plugins_.contains(plugin_id)) {
                        LOGE("Load plugin failed, plugin_id: {}, dll path: {}, errorString: repeated loading",
                             plugin_id,
                             StringUtil::ToUTF8(target_plugin_path));
                        continue;
                    }

                    auto filename = entry.path().filename().string();
                    auto param = GrPluginParam {
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

                    auto config_filepath = plugin_dir / (filename + ".toml");
                    if (std::filesystem::exists(config_filepath)) {
                        try {
                            auto cfg = toml::parse_file(config_filepath.u8string());
                            cfg.for_each([&](auto& k, auto& v) {
                                auto str_key = (std::string)k;
                                if constexpr (toml::is_string<decltype(v)>) {
                                    auto str_value = toml::value<std::string>(v).get();
                                    param.cluster_.insert({str_key, str_value});
                                }
                                else if constexpr (toml::is_boolean<decltype(v)>) {
                                    auto bool_value = toml::value<bool>(v).get();
                                    param.cluster_.insert({str_key, bool_value});
                                }
                                else if constexpr (toml::is_integer<decltype(v)>) {
                                    auto int_value = toml::value<int64_t>(v).get();
                                    param.cluster_.insert({str_key, int_value});
                                }
                                else if constexpr (toml::is_floating_point<decltype(v)>) {
                                    auto float_value = toml::value<double>(v).get();
                                    param.cluster_.insert({str_key, float_value});
                                }
                            });
                        } catch (const std::exception& e) {
                            LOGE("Load plugin failed, plugin_id: {}, dll path: {}, errorString: parse config {} failed: {}",
                                 plugin_id,
                                 StringUtil::ToUTF8(target_plugin_path),
                                 StringUtil::ToUTF8(config_filepath.wstring()),
                                 e.what());
                        }
                    } else {
                        LOGE("Load plugin failed, plugin_id: {}, dll path: {}, errorString: config {} does not exist",
                             plugin_id,
                             StringUtil::ToUTF8(target_plugin_path),
                             StringUtil::ToUTF8(config_filepath.wstring()));
                        continue;
                    }

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
        std::vector<GrPluginInterface*> plugins;
        VisitAllPlugins([&](GrPluginInterface* plugin) {
            plugins.push_back(plugin);
            if (plugin->GetPluginType() != GrPluginType::kNet) {
                VisitNetPlugins([=, this](GrNetPlugin* np) {
                    plugin->AttachNetPlugin(np->GetPluginId(), np);
                });
            }
        });

        // total plugins
        VisitAllPlugins([&](GrPluginInterface* plugin) {
            for (GrPluginInterface* p : plugins) {
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
        VisitAllPlugins([&](GrPluginInterface* plugin) {
            plugin->RegisterEventCallback([weak_self](const std::shared_ptr<GrPluginBaseEvent>& event) {
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
        std::map<std::string, GrPluginInterface*> plugins;
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

    GrPluginInterface* PluginManager::GetPluginById(const std::string& id) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        if (!plugins_.contains(id)) {
            return nullptr;
        }
        return plugins_.at(id);
    }

    GrVideoEncoderPlugin* PluginManager::GetFFmpegEncoderPlugin() {
        auto plugin = GetPluginById(kFFmpegEncoderPluginId);
        if (plugin) {
            return (GrVideoEncoderPlugin*)plugin;
        }
        return nullptr;
    }

    GrVideoEncoderPlugin* PluginManager::GetNvencEncoderPlugin() {
        auto plugin = GetPluginById(kNvencEncoderPluginId);
        if (plugin) {
            return (GrVideoEncoderPlugin*)plugin;
        }
        return nullptr;
    }

    GrVideoEncoderPlugin* PluginManager::GetAmfEncoderPlugin() {
        auto plugin = GetPluginById(kAmfEncoderPluginId);
        if (plugin) {
            return (GrVideoEncoderPlugin*)plugin;
        }
        return nullptr;
    }

    GrMonitorCapturePlugin* PluginManager::GetDDACapturePlugin() {
        auto plugin = GetPluginById(kDdaCapturePluginId);
        if (plugin) {
            return (GrMonitorCapturePlugin*)plugin;
        }
        return nullptr;
    }

    GrMonitorCapturePlugin* PluginManager::GetGdiCapturePlugin() {
        auto plugin = GetPluginById(kGdiCapturePluginId);
        if (plugin) {
            return (GrMonitorCapturePlugin*)plugin;
        }
        return nullptr;
    }

    GrDataProviderPlugin* PluginManager::GetMockVideoStreamPlugin() {
        auto plugin = GetPluginById(kMockVideoStreamPluginId);
        if (plugin) {
            return (GrDataProviderPlugin*)plugin;
        }
        return nullptr;
    }

    GrDataProviderPlugin* PluginManager::GetAudioCapturePlugin() {
        auto plugin = GetPluginById(kWasAudioCapturePluginId);
        if (plugin) {
            return (GrDataProviderPlugin*)plugin;
        }
        return nullptr;
    }

    GrAudioEncoderPlugin* PluginManager::GetAudioEncoderPlugin() {
        auto plugin = GetPluginById(kOpusEncoderPluginId);
        if (plugin) {
            return (GrAudioEncoderPlugin*)plugin;
        }
        return nullptr;
    }

    GrPluginInterface* PluginManager::GetFileTransferPlugin() {
        auto plugin = GetPluginById(kNetFileTransferPluginId);
        if (plugin) {
            return (GrPluginInterface*)plugin;
        }
        return nullptr;
    }

    GrNetPlugin* PluginManager::GetUdpPlugin() {
        auto plugin = GetPluginById(kNetUdpPluginId);
        if (plugin) {
            return (GrNetPlugin*)plugin;
        }
        return nullptr;
    }

    GrPluginInterface* PluginManager::GetClipboardPlugin() {
        auto plugin = GetPluginById(kClipboardPluginId);
        if (plugin) {
            return (GrPluginInterface*)plugin;
        }
        return nullptr;
    }

    GrPluginInterface* PluginManager::GetRtcPlugin() {
        auto plugin = GetPluginById(kNetRtcPluginId);
        if (plugin) {
            return plugin;
        }
        return nullptr;
    }

    GrNetPlugin* PluginManager::GetRelayPlugin() {
        auto plugin = GetPluginById(kRelayPluginId);
        if (plugin) {
            return (GrNetPlugin*)plugin;
        }
        return nullptr;
    }

    GrFrameCarrierPlugin* PluginManager::GetFrameCarrierPlugin() {
        auto plugin = GetPluginById(kFrameCarrierPluginId);
        if (plugin) {
            return (GrFrameCarrierPlugin*)plugin;
        }
        return nullptr;
    }

    GrFrameProcessorPlugin* PluginManager::GetFrameResizePlugin() {
        auto plugin = GetPluginById(kFrameResizerPluginId);
        if (plugin) {
            return (GrFrameProcessorPlugin*)plugin;
        }
        return nullptr;
    }

    GrPluginInterface* PluginManager::GetEventsReplayerPlugin() {
        auto plugin = GetPluginById(kEventReplayerPluginId);
        if (plugin) {
            return plugin;
        }
        return nullptr;
    }

    GrPluginInterface* PluginManager::GetRtcLocalPlugin() {
        auto plugin = GetPluginById(kNetRtcLocalPluginId);
        if (plugin) {
            return plugin;
        }
        return nullptr;
    }

    void PluginManager::VisitAllPlugins(const std::function<void(GrPluginInterface *)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (visitor) {
                visitor(plugin);
            }
        }
    }

    void PluginManager::VisitStreamPlugins(const std::function<void(GrStreamPlugin *)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (plugin->GetPluginType() == GrPluginType::kStream) {
                visitor((GrStreamPlugin *) plugin);
            }
        }
    }

    void PluginManager::VisitUtilPlugins(const std::function<void(GrPluginInterface *)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (plugin->GetPluginType() == GrPluginType::kUtil) {
                visitor(plugin);
            }
        }
    }

    void PluginManager::VisitEncoderPlugins(const std::function<void(GrVideoEncoderPlugin*)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (plugin->GetPluginType() == GrPluginType::kEncoder) {
                visitor((GrVideoEncoderPlugin *) plugin);
            }
        }
    }

    void PluginManager::VisitNetPlugins(const std::function<void(GrNetPlugin*)>&& visitor) {
        std::shared_lock<std::shared_mutex> lock(plugins_mtx_);
        for (const auto& [k, plugin] : plugins_) {
            if (plugin->GetPluginType() == GrPluginType::kNet) {
                visitor((GrNetPlugin*) plugin);
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

            std::vector<GrPluginInterface*> plugins_snapshot;
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
                if (plugin->GetPluginType() == GrPluginType::kNet) {
                    connected_client_count += ((GrNetPlugin*)plugin)->GetConnectedClientsCount();
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
        VisitAllPlugins([&](GrPluginInterface *plugin) {
            LOGI("Plugin {}. [{}] vn: [{}], vc: [{}], enabled: [{}]",
                 index++,
                 plugin->GetPluginName(),
                 plugin->GetVersionName(),
                 plugin->GetVersionCode(),
                 plugin->IsPluginEnabled()
            );
        });
    }

    void PluginManager::SyncPluginSettingsInfo(const GrPluginSettingsInfo& info) {
        if (exiting_) {
            return;
        }
        VisitAllPlugins([&](GrPluginInterface* plugin) {
            plugin->OnSyncPluginSettingsInfo(info);
        });
    }

    int64_t PluginManager::GetQueuingMediaMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        VisitNetPlugins([&](GrNetPlugin* plugin) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingMediaMsgCount();
            }
        });
        return queuing_msg_count;
    }

    int64_t PluginManager::GetQueuingFtMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        VisitNetPlugins([&](GrNetPlugin* plugin) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingFtMsgCount();
            }
        });
        return queuing_msg_count;
    }

    int PluginManager::GetTotalConnectedClientsCount() {
        int total_size = 0;
        VisitNetPlugins([&](GrNetPlugin* plugin) {
            total_size += plugin->GetConnectedClientsCount();
        });
        return total_size;
    }

    std::vector<std::shared_ptr<GrConnectedClientInfo>> PluginManager::GetConnectedClientsInfo() {
        std::vector<std::shared_ptr<GrConnectedClientInfo>> clients_info;
        VisitNetPlugins([&](GrNetPlugin* plugin) {
            if (auto cs = plugin->GetConnectedClientInfo(); !cs.empty()) {
                for (auto& info : cs) {
                    clients_info.push_back(info);
                }
            }
        });
        return clients_info;
    }

    // is GDI
    bool PluginManager::IsGDIMonitorCapturePlugin(GrMonitorCapturePlugin* plugin) {
        return plugin && plugin->GetPluginId() == kGdiCapturePluginId;
    }

    // is DDA
    bool PluginManager::IsDDAMonitorCapturePlugin(GrMonitorCapturePlugin* plugin) {
        return plugin && plugin->GetPluginId() == kDdaCapturePluginId;
    }

}
