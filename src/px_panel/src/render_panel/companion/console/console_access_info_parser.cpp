//
// Created by RGAA on 20/10/2025.
//

#include "console_access_info_parser.h"
#include <nlohmann/json.hpp>
#include "px_common/log.h"
#include "render_panel/companion/panel_companion.h"

using namespace nlohmann;

namespace px
{

    // "console_srv_config": {
    //     "srv_name": "Srv.01",
    //     "srv_w3c_ip": "127.0.0.1",
    //     "srv_console_port": 30500,
    //     "srv_udp_broadcast_port": 30501,
    //     "srv_relay_port": 30502,
    //     "srv_appkey": "ff785bd3031bc6cf920a782e50f43dcb",
    //     "srv_ssl_enable": true
    // }

    std::shared_ptr<ConsoleAccessInfo> ConsoleAccessInfoParser::ParseInfo(const std::string& info) {
        try {
            auto ac_info = std::make_shared<ConsoleAccessInfo>();
            auto obj = json::parse(info);
            const bool canonical = obj.contains("console_srv_config");
            auto console_obj = canonical ? obj["console_srv_config"] : obj["cms_srv_config"];
            ac_info->console_config_.srv_name_ = console_obj["srv_name"].get<std::string>();
            ac_info->console_config_.srv_w3c_ip_ = console_obj["srv_w3c_ip"].get<std::string>();
            ac_info->console_config_.srv_console_port_ = canonical
                ? console_obj["srv_console_port"].get<int>()
                : console_obj["srv_cms_port"].get<int>();
            ac_info->console_config_.srv_relay_port_ = console_obj["srv_relay_port"].get<int>();
            ac_info->console_config_.srv_appkey_ = console_obj["srv_appkey"].get<std::string>();
            // default true, old deployments don't broadcast this field
            ac_info->console_config_.srv_ssl_enable_ = console_obj.value("srv_ssl_enable", true);
            return ac_info;
        }
        catch (std::exception& e) {
            LOGE("Parse console access error: {}, info: {}", e.what(), info);
            return nullptr;
        }
    }

}
