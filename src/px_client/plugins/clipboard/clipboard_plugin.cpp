//
// Created RGAA on 15/11/2024.
//

#include "clipboard_plugin.h"
#include "px_message.pb.h"
#include "px_common_new/log.h"
#include "px_common_new/file.h"
#include "px_common_new/image.h"
#include "px_client/plugin_interface/ct_plugin_ids.h"
#include "px_client/plugin_interface/ct_plugin_events.h"
#include "px_client/plugin_interface/ct_app_events.h"
#include "ct_clipboard_manager.h"
#include "clipboard_runtime_bridge.h"
#include "px_client/plugin_interface/ct_plugin_context.h"
#include "px_common_new/md5.h"
#include "px_message_new/proto_converter.h"

namespace px
{

    std::string ClientClipboardPlugin::GetPluginId() {
        return kClientClipboardPluginId;
    }

    std::string ClientClipboardPlugin::GetPluginName() {
        return "Clipboard";
    }

    std::string ClientClipboardPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t ClientClipboardPlugin::GetVersionCode() {
        return 110;
    }

    void ClientClipboardPlugin::On1Second() {
        ClientPluginInterface::On1Second();
    }
    
    bool ClientClipboardPlugin::OnCreate(const px::ClientPluginParam& param) {
        ClientPluginInterface::OnCreate(param);
        plugin_type_ = ClientPluginType::kUtil;

        if (!IsPluginEnabled()) {
            return true;
        }
        root_widget_->hide();

        runtime_bridge_ = std::make_shared<ClipboardRuntimeBridge>(
            plugin_settings_, MakeQueuedEventDispatcher());
        clipboard_mgr_ = std::make_shared<ClipboardManager>(runtime_bridge_);
        if (!clipboard_mgr_->Start()) {
            clipboard_mgr_.reset();
            runtime_bridge_->Deactivate();
            runtime_bridge_.reset();
            ClientPluginInterface::OnDestroy();
            return false;
        }

        return true;
    }

    bool ClientClipboardPlugin::OnDestroy() {
        if (runtime_bridge_) {
            runtime_bridge_->Deactivate();
        }
        if (clipboard_mgr_) {
            clipboard_mgr_->Stop();
            clipboard_mgr_.reset();
        }
        runtime_bridge_.reset();
        return ClientPluginInterface::OnDestroy();
    }

    bool ClientClipboardPlugin::OnStop() {
        const bool stopped = ClientPluginInterface::OnStop();
        if (clipboard_mgr_) {
            clipboard_mgr_->Stop();
        }
        return stopped;
    }

    void ClientClipboardPlugin::OnMessage(std::shared_ptr<Message> msg) {
        ClientPluginInterface::OnMessage(msg);
        if (msg->type() == MessageType::kClipboardInfo) {
            if (clipboard_mgr_) {
                clipboard_mgr_->OnRemoteClipboardMessage(msg);
            }
        }
        else if (msg->type() == px::kClipboardInfoResp) {
            if (clipboard_mgr_) {
                clipboard_mgr_->OnRemoteClipboardRespMessage(msg);
            }
        }
        else if (msg->type() == px::kClipboardReqAtBegin) {
            // begin; server -> client
            // copy files from client -> server
            const auto bridge = runtime_bridge_;
            plugin_context_->PostWorkTask([bridge, msg]() {
                if (bridge) {
                    bridge->OnRequestFileBegin(msg);
                }
            });
        }
        else if (msg->type() == px::kClipboardReqBuffer) {
            // transferring
            // server -> request a part of data in the file -> client -> response -> server
            const auto bridge = runtime_bridge_;
            plugin_context_->PostWorkTask([bridge, msg]() {
                if (bridge) {
                    bridge->OnRequestFileBuffer(msg);
                }
            });
        }
        else if (msg->type() == px::kClipboardReqAtEnd) {
            // end; server -> client
            // copy files from client -> server
            const auto bridge = runtime_bridge_;
            plugin_context_->PostWorkTask([bridge, msg]() {
                if (bridge) {
                    bridge->OnRequestFileEnd(msg);
                }
            });
        }
        else if (msg->type() == MessageType::kClipboardRespBuffer) {
            // server -> response a part of data in the file -> client
            const auto manager = clipboard_mgr_;
            plugin_context_->PostWorkTask([manager, msg]() {
                if (manager) {
                    manager->OnRemoteFileRespMessage(msg);
                }
            });
        }
    }

    void ClientClipboardPlugin::DispatchAppEvent(const std::shared_ptr<ClientAppBaseEvent> &event) {
        ClientPluginInterface::DispatchAppEvent(event);
    }

    void ClientClipboardPlugin::SyncClientPluginSettings(
        const ClientPluginSettings& settings) {
        ClientPluginInterface::SyncClientPluginSettings(settings);
        if (runtime_bridge_) {
            runtime_bridge_->UpdateSettings(plugin_settings_);
        }
    }

    void ClientClipboardPlugin::OnLocalClipboardUpdated() {
        if (clipboard_mgr_) {
            clipboard_mgr_->OnLocalClipboardUpdated();
        }
    }

    bool ClientClipboardPlugin::IsClipboardEnabled() {
        return runtime_bridge_ && runtime_bridge_->IsEnabled();
    }
}

PX_PLUGIN_EXPORT(px::ClientClipboardPlugin)
