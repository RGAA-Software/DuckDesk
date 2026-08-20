//
// Created by RGAA on 12/06/2025.
//

#include "relay_api.h"
#include "px_common_new/log.h"
#include "px_common_new/http_client.h"
#include <nlohmann/json.hpp>
#include "relay_message.pb.h"

using namespace px;
using namespace nlohmann;

namespace px_relay
{

    // Ping
    px::Result<bool, int> RelayApi::Ping(const std::string& host, int port, const std::string& appkey) {
        auto client = HttpClient::Make(host, port, kRelayPing, 3000);
        auto resp = client->Request({
            {"appkey", appkey}
        });
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("GetRelayDeviceInfo failed : {}", resp.status);
            return TRError(resp.status);
        }

        // {
        //     "code": 200,
        //     "message": "ok",
        //     "data": "Pong"
        // }
        try {
            auto obj = json::parse(resp.body);
            auto code = obj["code"].get<int>();
            auto data = obj["data"].get<std::string>();
            return code == 200 && data == "Pong";
        }
        catch (const std::exception& e) {
            LOGE("Ping Exception: {}, body: {}", e.what(), resp.body);
            return TRError(kRelayParseJsonFailed);
        }
    }

    //
    px::Result<std::shared_ptr<RelayDeviceInfo>, int>
    RelayApi::GetRelayDeviceInfo(const std::string& host,
                                 int port,
                                 const std::string& device_id,
                                 const std::string& appkey) {
        auto client = HttpClient::Make(host, port, kRelayGetDeviceInfo, 3000);
        auto resp = client->Request({
            {"device_id", device_id},
            {"appkey", appkey}
        });
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("GetRelayDeviceInfo failed, status: {}, host: {}:{}", resp.status, host, port);
            return TRError(resp.status);
        }

        try {
            //LOGI("==> RelayDevice body: {}", resp.body);
            auto obj = json::parse(resp.body);
            auto code = obj["code"].get<int>();
            if (code != 200) {
                LOGE("GetDevice info failed: {}", code);
                return TRError(code);
            }
            // "device_local_ips": "110.0.0.16;192.168.56.1;",
            // "relay_server_ip": "139.171.84.236",
            // "device_w3c_ip": "139.171.84.236",
            // "device_id": "server_488050198",
            // "relay_server_port": "40302"
            auto device_w3c_ip = obj["data"]["device_w3c_ip"].get<std::string>();
            auto relay_server_ip = obj["data"]["relay_server_ip"].get<std::string>();
            auto relay_server_port = std::atoi(obj["data"]["relay_server_port"].get<std::string>().c_str());

            auto info = std::make_shared<RelayDeviceInfo>();
            info->set_device_id(device_id);
            info->set_device_w3c_ip(device_w3c_ip);
            info->set_relay_server_ip(relay_server_ip);
            info->set_relay_server_port(relay_server_port);
            return info;
        } catch(std::exception& e) {
            LOGE("GetRelayDeviceInfo Exception: {}", e.what());
            return TRError(kRelayParseJsonFailed);
        }
    }

    px::Result<int, int> RelayApi::NotifyEvent(const std::string& host,
                                               int port,
                                               const std::string& from_device_id, // this device
                                               const std::string& to_device_id,   // remote device, id starts with: server_
                                               const std::string& event,
                                               const std::string& appkey) {
        auto client = HttpClient::Make(host, port, kRelayNotifyEvent, 3000);
        auto resp = client->Post({
            {"from_device_id", from_device_id},
            {"to_device_id", to_device_id},
            {"appkey", appkey}
        }, event);
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("NotifyEvent failed, host: {}, port: {}, path: {}, code: {}", host, port, kRelayNotifyEvent, resp.status);
            return TRError(resp.status);
        }

        try {
            auto obj = json::parse(resp.body);
            auto code = obj["code"].get<int>();
            if (code != 200) {
                LOGE("GetDevice info failed: {}", code);
                return TRError(code);
            }
            return kRelayOk;
        } catch(std::exception& e) {
            LOGE("NotifyEvent exception: {}, body: {}", e.what(), resp.body);
            return TRError(kRelayParseJsonFailed);
        }
    }

    bool RelayApi::IsRelayDeviceValid(const std::shared_ptr<RelayDeviceInfo>& info) {
        return !info->device_id().empty();
    }

}
