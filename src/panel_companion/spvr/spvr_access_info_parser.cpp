//
// Created by RGAA on 20/10/2025.
//

#include "spvr_access_info_parser.h"
#include "tc_3rdparty/json/json.hpp"
#include "tc_common_new/log.h"
#include "render_panel/companion/panel_companion.h"

using namespace nlohmann;

namespace tc
{

    //{
    //    "spvr_srv_config": {
    //        "srv_id": "",
    //        "srv_name": "Srv.Supervisor.01",
    //        "srv_type": "spvr",
    //        "srv_w3c_ip": "192.168.1.111",
    //        "srv_working_port": 30500,
    //        "srv_grpc_port": 0,
    //        "srv_appkey": "ff785bd3031bc6cf920a782e50f43dcb"
    //    },
    //    "relay_srv_config": [
    //        {
    //            "srv_id": "",
    //            "srv_name": "Srv.Relay.01",
    //            "srv_type": "relay",
    //            "srv_w3c_ip": "192.168.1.111",
    //            "srv_working_port": 30600,
    //            "srv_grpc_port": 40600,
    //            "srv_appkey": "ff785bd3031bc6cf920a782e50f43dcb"
    //        }
    //    ]
    //}

    std::shared_ptr<SpvrAccessInfo> SpvrAccessInfoParser::ParseInfo(const std::string& info) {
        try {
            auto ac_info = std::make_shared<SpvrAccessInfo>();
            auto obj = json::parse(info);
            {
                auto spvr_obj = obj["spvr_srv_config"];
                ac_info->spvr_config_.srv_name_ = spvr_obj["srv_name"].get<std::string>();
                ac_info->spvr_config_.srv_type_ = spvr_obj["srv_type"].get<std::string>();
                ac_info->spvr_config_.srv_w3c_ip_ = spvr_obj["srv_w3c_ip"].get<std::string>();
                ac_info->spvr_config_.srv_working_port_ = spvr_obj["srv_working_port"].get<int>();
                ac_info->spvr_config_.srv_appkey_ = spvr_obj["srv_appkey"].get<std::string>();
            }

            auto relay_objs = obj["relay_srv_config"];
            for (const auto& relay_obj : relay_objs) {
                auto relay_config = RelaySrvConfig{};
                relay_config.srv_name_ = relay_obj["srv_name"].get<std::string>();
                relay_config.srv_type_ = relay_obj["srv_type"].get<std::string>();
                relay_config.srv_w3c_ip_ = relay_obj["srv_w3c_ip"].get<std::string>();
                relay_config.srv_working_port_ = relay_obj["srv_working_port"].get<int>();
                relay_config.srv_appkey_ = relay_obj["srv_appkey"].get<std::string>();
                ac_info->relay_configs_.push_back(relay_config);
            }

            return ac_info;
        }
        catch (std::exception& e) {
            LOGE("Parse spvr access error: {}, info: {}", e.what(), info);
            return nullptr;
        }
    }

}