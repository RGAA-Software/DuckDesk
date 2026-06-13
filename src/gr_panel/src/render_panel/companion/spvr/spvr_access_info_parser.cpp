//
// Created by RGAA on 20/10/2025.
//

#include "spvr_access_info_parser.h"
#include <nlohmann/json.hpp>
#include "tc_common_new/log.h"
#include "render_panel/companion/panel_companion.h"

using namespace nlohmann;

namespace tc
{

    // "spvr_srv_config": {
    //     "srv_name": "Srv.01",
    //     "srv_w3c_ip": "127.0.0.1",
    //     "srv_spvr_port": 30500,
    //     "srv_udp_broadcast_port": 30501,
    //     "srv_relay_port": 30502,
    //     "srv_appkey": "ff785bd3031bc6cf920a782e50f43dcb"
    // }

    std::shared_ptr<SpvrAccessInfo> SpvrAccessInfoParser::ParseInfo(const std::string& info) {
        try {
            auto ac_info = std::make_shared<SpvrAccessInfo>();
            auto obj = json::parse(info);
            auto spvr_obj = obj["spvr_srv_config"];
            ac_info->spvr_config_.srv_name_ = spvr_obj["srv_name"].get<std::string>();
            ac_info->spvr_config_.srv_w3c_ip_ = spvr_obj["srv_w3c_ip"].get<std::string>();
            ac_info->spvr_config_.srv_spvr_port_ = spvr_obj["srv_spvr_port"].get<int>();
            ac_info->spvr_config_.srv_relay_port_ = spvr_obj["srv_relay_port"].get<int>();
            ac_info->spvr_config_.srv_appkey_ = spvr_obj["srv_appkey"].get<std::string>();
            return ac_info;
        }
        catch (std::exception& e) {
            LOGE("Parse spvr access error: {}, info: {}", e.what(), info);
            return nullptr;
        }
    }

}