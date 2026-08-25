//
// Created by RGAA on 20/05/2025.
//

#include "conn_info_parser.h"
#include <nlohmann/json.hpp>
#include "px_console_client/console_stream.h"
#include "px_common_new/log.h"
#include "px_common_new/base64.h"

using namespace nlohmann;

namespace px
{

    std::shared_ptr<PxConnectionInfo> ConnInfoParser::Parse(const std::string& info) {
        std::string prefix = "link://";
        if (!info.starts_with(prefix)) {
            return nullptr;
        }
        auto target_info = info.substr(prefix.size());
        target_info = Base64::Base64Decode(target_info);

        try {
            auto conn_info = std::make_shared<PxConnectionInfo>();
            auto obj = json::parse(target_info);

            // device_id
            conn_info->device_id_ = obj["did"].get<std::string>();
            // device name
            if (!obj["dn"].is_null()) {
                conn_info->device_name_ = obj["dn"].get<std::string>();
            }
            // random password
            conn_info->random_pwd_ = obj["rpwd"].get<std::string>();
            // icon index
            conn_info->icon_idx_ = obj["iidx"].get<int>();
            // ips
            auto ips_array = obj["ips"];
            if (ips_array.is_array()) {
                for (const auto& item : ips_array) {
                    PxConnectionInfo::PxConnectionHost host;
                    host.ip_ = item["ip"].get<std::string>();
                    //host.type_ = item["type"].get<std::string>();
                    conn_info->hosts_.push_back(host);
                }
            }

            // panel server port
            conn_info->panel_srv_port_ = obj["ppt"].get<int>();
            // render server port
            conn_info->render_srv_port_ = obj["rdpt"].get<int>();

            // relay host
            if (!obj["rlst"].is_null()) {
                conn_info->relay_host_ = obj["rlst"].get<std::string>();
            }

            // relay port
            if (!obj["rlpt"].is_null()) {
                conn_info->relay_port_ = obj["rlpt"].get<int>();
            }

            // relay appkey
            if (!obj["rlak"].is_null()) {
                conn_info->relay_appkey_ = obj["rlak"].get<std::string>();
            }

            LOGI("Parsed link connection metadata: device_id={}, endpoints={}, render_port={}",
                 conn_info->device_id_, conn_info->hosts_.size(), conn_info->render_srv_port_);
            return conn_info;
        }
        catch(std::exception& e) {
            LOGE("Parse link:// failed: {}", e.what());
            return nullptr;
        }
    }

}
