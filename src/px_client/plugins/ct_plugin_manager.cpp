//
// Created by RGAA on 15/11/2024.
//

#include "ct_plugin_manager.h"
#include "px_common_new/log.h"
#include <QDir>
#include <QFile>
#include <QApplication>
#include "ct_settings.h"
#include "ct_plugin_ids.h"
#include "ct_client_context.h"
#include "ct_base_workspace.h"
#include "ct_plugin_event_router.h"
#include "px_common_new/folder_util.h"
#include "px_client/plugin_interface/ct_plugin_interface.h"

typedef void *(*FnGetInstance)();

namespace px
{

    std::shared_ptr<ClientPluginManager> ClientPluginManager::Make(const std::shared_ptr<BaseWorkspace>& ws) {
        return std::make_shared<ClientPluginManager>(ws);
    }

    ClientPluginManager::ClientPluginManager(const std::shared_ptr<BaseWorkspace>& ws) {
        this->ws_ = ws;
        this->context_ = ws_->GetContext();
    }

    void ClientPluginManager::LoadAllPlugins() {
        exiting_ = false;
        auto base_path = QCoreApplication::applicationDirPath();
        auto base_data_path =FolderUtil::GetProgramDataPath();
        LOGI("plugin base path: {}", base_path.toStdString());
        QDir plugin_dir(base_path + R"(/deps/ct_plugins)");
        QStringList filters;
        filters << QString("*%1").arg(".dll");
        plugin_dir.setNameFilters(filters);

        auto entryInfoList = plugin_dir.entryInfoList();
        for (const auto &info: entryInfoList) {
            auto target_plugin_path = base_path + R"(/deps/ct_plugins/)" + info.fileName();
            auto plugin_id = info.baseName().toStdString();
            LOGI("Will load: {}", target_plugin_path.toStdString());

            auto library = new QLibrary(target_plugin_path);
            if (!library->load()) {
                LOGE("Load client plugin failed, plugin_id: {}, dll path: {}, errorString: {}",
                     plugin_id,
                     target_plugin_path.toStdString(),
                     library->errorString().toStdString());
                continue;
            }
            auto fn_get_instance = (FnGetInstance)library->resolve("GetInstance");

            auto func = (FnGetInstance) fn_get_instance;
            if (func) {
                auto plugin = (ClientPluginInterface*)func();
                if (plugin) {
                    plugin_id = plugin->GetPluginId();
                    if (Settings::Instance()->file_transfer_only_ && plugin_id != kClientFtPluginId) {
                        library->unload();
                        continue;
                    }
                    if (plugins_.contains(plugin_id)) {
                        LOGE("Load client plugin failed, plugin_id: {}, dll path: {}, errorString: repeated loading",
                             plugin_id,
                             target_plugin_path.toStdString());
                        continue;
                    }

                    auto settings = px::Settings::Instance();

                    // create it
                    auto filename = info.fileName();
                    auto param = ClientPluginParam {
                        .cluster_ = {
                            {"name", filename.toStdString()},
                            {"base_path", base_path.toStdString()},
                            {"base_data_path", base_data_path},
                            {"screen_recording_path", settings->screen_recording_path_},
                            {"clipboard_enabled", settings->clipboard_on_},
                            {"device_id", settings->device_id_.empty() ? settings->my_host_ : settings->device_id_},
                            {"stream_id", settings->stream_id_},
                            {"language", (int64_t)settings->language_},
                            {"stream_name", settings->stream_name_},
                            {"display_name", settings->display_name_},
                            {"display_remote_name", settings->display_remote_name_},
                        },
                    };

                    if (!plugin->OnCreate(param)) {
                        LOGE("Load client plugin failed, plugin_id: {}, dll path: {}, errorString: OnCreate failed for {}",
                             plugin_id,
                             target_plugin_path.toStdString(),
                             plugin->GetPluginName());
                        continue;
                    }

                    if (!plugin->IsPluginEnabled()) {
                        LOGW("Plugin: {} is disabled!", plugin->GetPluginName());
                    }

                    plugins_.insert({plugin_id, plugin});

                    LOGI("{} loaded, version: {}", plugin->GetPluginName(), plugin->GetVersionName());
                } else {
                    LOGE("Load client plugin failed, plugin_id: {}, dll path: {}, errorString: object create failed",
                         plugin_id,
                         target_plugin_path.toStdString());
                }
            } else {
                LOGE("Load client plugin failed, plugin_id: {}, dll path: {}, errorString: cannot resolve GetInstance: {}",
                     plugin_id,
                     target_plugin_path.toStdString(),
                     library->errorString().toStdString());
            }
        }
    }

    void ClientPluginManager::RegisterPluginEventsCallback() {
        this->evt_router_ = std::make_shared<ClientPluginEventRouter>(ws_);
        auto weak_self = weak_from_this();
        VisitAllPlugins([&](ClientPluginInterface* plugin) {
            plugin->RegisterEventCallback([weak_self](const std::shared_ptr<ClientPluginBaseEvent>& event) {
                auto self = weak_self.lock();
                if (!self || self->exiting_ || !self->evt_router_) {
                    return;
                }
                self->evt_router_->ProcessPluginEvent(event);
            });
        });
    }

    void ClientPluginManager::ReleaseAllPlugins() {
        exiting_ = true;
        for (const auto& [k, plugin] : plugins_) {
            plugin->OnStop();
        }
        for (const auto& [k, plugin] : plugins_) {
            plugin->OnDestroy();
        }
        plugins_.clear();
        evt_router_.reset();
    }

    void ClientPluginManager::ReleasePlugin(const std::string &name) {

    }

    ClientPluginInterface* ClientPluginManager::GetPluginById(const std::string& id) {
        if (!plugins_.contains(id)) {
            return nullptr;
        }
        return plugins_.at(id);
    }

    MediaRecordPluginClientInterface* ClientPluginManager::GetMediaRecordPlugin() {
        auto plugin = GetPluginById(kClientMediaRecordPluginId);
        if (plugin) {
            return (MediaRecordPluginClientInterface*)plugin;
        }
        return nullptr;
    }

    ClientClipboardPlugin* ClientPluginManager::GetClipboardPlugin() {
        auto plugin = GetPluginById(kClientClipboardPluginId);
        if (plugin) {
            return (ClientClipboardPlugin*)plugin;
        }
        return nullptr;
    }

    ClientPluginInterface* ClientPluginManager::GetFileTransferPlugin() {
        return GetPluginById(kClientFtPluginId);
    }

    void ClientPluginManager::VisitAllPlugins(const std::function<void(ClientPluginInterface *)>&& visitor) {
        for (const auto& [k, plugin] : plugins_) {
            if (visitor) {
                visitor(plugin);
            }
        }
    }

    void ClientPluginManager::On1Second() {
        if (exiting_ || !context_) {
            return;
        }
        auto weak_self = weak_from_this();
        context_->PostTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->context_ || self->plugins_.empty()) {
                return;
            }
            self->VisitAllPlugins([self](ClientPluginInterface* plugin) {
                if (self->exiting_) {
                    return;
                }
                plugin->On1Second();
            });
        });
    }

    void ClientPluginManager::DumpPluginInfo() {
        LOGI("====> Total plugins: {}", plugins_.size());
        int index = 1;
        VisitAllPlugins([&](ClientPluginInterface *plugin) {
            LOGI("Plugin {}. [{}] vn: [{}], vc: [{}], enabled: [{}]",
                 index++,
                 plugin->GetPluginName(),
                 plugin->GetVersionName(),
                 plugin->GetVersionCode(),
                 plugin->IsPluginEnabled()
            );
        });
    }
}
