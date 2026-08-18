//
// Created by RGAA on 20/10/2025.
//

#include "cms_access_info_parser.h"
#include <nlohmann/json.hpp>
#include "px_common_new/log.h"
#include "render_panel/companion/panel_companion.h"

using namespace nlohmann;

namespace px
{

    // "cms_srv_config": {
    //     "srv_name": "Srv.01",
    //     "srv_w3c_ip": "127.0.0.1",
    //     "srv_cms_port": 30500,
    //     "srv_udp_broadcast_port": 30501,
    //     "srv_relay_port": 30502,
    //     "srv_appkey": "ff785bd3031bc6cf920a782e50f43dcb",
    //     "srv_ssl_enable": true
    // }

    std::shared_ptr<CmsAccessInfo> CmsAccessInfoParser::ParseInfo(const std::string& info) {
        try {
            auto ac_info = std::make_shared<CmsAccessInfo>();
            auto obj = json::parse(info);
            auto cms_obj = obj["cms_srv_config"];
            ac_info->cms_config_.srv_name_ = cms_obj["srv_name"].get<std::string>();
            ac_info->cms_config_.srv_w3c_ip_ = cms_obj["srv_w3c_ip"].get<std::string>();
            ac_info->cms_config_.srv_cms_port_ = cms_obj["srv_cms_port"].get<int>();
            ac_info->cms_config_.srv_relay_port_ = cms_obj["srv_relay_port"].get<int>();
            ac_info->cms_config_.srv_appkey_ = cms_obj["srv_appkey"].get<std::string>();
            // default true, old deployments don't broadcast this field
            ac_info->cms_config_.srv_ssl_enable_ = cms_obj.value("srv_ssl_enable", true);
            return ac_info;
        }
        catch (std::exception& e) {
            LOGE("Parse cms access error: {}, info: {}", e.what(), info);
            return nullptr;
        }
    }

}