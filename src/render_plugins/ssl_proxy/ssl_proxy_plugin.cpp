//
// Created RGAA on 15/11/2024.
//

#include "ssl_proxy_plugin.h"
#include "plugin_interface/gr_plugin_events.h"
#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "tc_common_new/image.h"
#include "render/plugins/plugin_ids.h"
#include "ssl_proxy_server.h"

void* GetInstance() {
    static tc::SSLProxyPlugin plugin;
    return (void*)&plugin;
}

namespace tc
{

    std::string SSLProxyPlugin::GetPluginId() {
        return kSSLProxyPluginId;
    }

    std::string SSLProxyPlugin::GetPluginName() {
        return "SSL Proxy";
    }

    std::string SSLProxyPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t SSLProxyPlugin::GetVersionCode() {
        return 110;
    }

    std::string SSLProxyPlugin::GetPluginDescription() {
        return "Network proxy";
    }

    void SSLProxyPlugin::On1Second() {
        GrPluginInterface::On1Second();

    }
    
    bool SSLProxyPlugin::OnCreate(const tc::GrPluginParam &param) {
        GrPluginInterface::OnCreate(param);
        plugin_type_ = GrPluginType::kNet;

        if (!IsPluginEnabled()) {
            return true;
        }

        // net_ws plugin using this port
        auto listen_port = GetConfigIntParam("ws-listen-port");
        auto config_listen_port = GetConfigIntParam("listen-port");
        if (config_listen_port > 0) {
            listen_port = config_listen_port;
        }

        auto proxy_port = listen_port + 1;
        proxy_server_ = std::make_shared<SSLProxyServer>(this, (uint16_t)listen_port, (uint16_t)proxy_port);
        proxy_server_->Start();

        return true;
    }

    void SSLProxyPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {

    }

    bool SSLProxyPlugin::PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        return false;
    }

    bool SSLProxyPlugin::PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        return false;
    }

}
