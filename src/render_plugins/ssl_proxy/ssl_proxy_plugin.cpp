//
// Created RGAA on 15/11/2024.
//

#include "ssl_proxy_plugin.h"
#include "plugin_interface/gr_plugin_events.h"
#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "tc_common_new/image.h"
#include "render/plugins/plugin_ids.h"

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
        plugin_type_ = GrPluginType::kStream;

        if (!IsPluginEnabled()) {
            return true;
        }
        root_widget_->hide();
        //root_widget_->show();
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
