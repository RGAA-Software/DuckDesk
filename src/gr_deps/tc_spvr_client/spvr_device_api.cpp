//
// Created by RGAA on 26/03/2025.
//

#include "spvr_device_api.h"
#include "spvr_server_info.h"
#include "spvr_errors.h"
#include <nlohmann/json.hpp>
#include "spvr_device.h"
#include "tc_common_new/http_client.h"
#include "tc_common_new/log.h"
#include "tc_common_new/http_base_op.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/message_notifier.h"
#include "tc_common_new/hardware.h"
#include "tc_common_new/ip_util.h"
#include "tc_common_new/base64.h"
#include "tc_common_new/uuid.h"

using namespace tc;
using namespace nlohmann;

// ping
const std::string kSpvrPing = "/ping";

// /api/v1/device/control
const std::string kSpvrDeviceControl = "/api/v1/device/control";

// create new device
const std::string kApiRequestNewDevice = kSpvrDeviceControl + "/create/new/device";

// update random password
const std::string kApiUpdateRandomPwd = kSpvrDeviceControl + "/update/random/pwd";

// update safety password
const std::string kApiUpdateSafetyPwd = kSpvrDeviceControl + "/update/safety/pwd";

// get device by id
const std::string kApiQueryDeviceById = kSpvrDeviceControl + "/query/device/by/id";

// /api/v1/panel/control
const std::string kSpvrPanelControl = "/api/v1/panel/control";

// query the live panel connection of a device (device online check)
const std::string kApiQueryPanelConnByDeviceId = kSpvrPanelControl + "/query/panel/conn/by/device/id";

// update desktop link
const std::string kApiUpdateDesktopLink = kSpvrDeviceControl + "/update/desktop/link";

// update device name
const std::string kApiUpdateDeviceName = kSpvrDeviceControl + "/update/device/name";

// /append/used/time
const std::string kApiAppendUsedTime = kSpvrDeviceControl + "/append/used/time";

namespace spvr
{

    // Ping
    tc::Result<bool, SpvrApiError> SpvrDeviceApi::Ping(const std::string& host, int port, const std::string& appkey) {
        auto client = HttpClient::MakeSSL(host, port, kSpvrPing, 3000);
        auto resp = client->Request({
            {"appkey", appkey}
        });
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("GetRelayDeviceInfo failed : {}", resp.status);
            return TRError((SpvrApiError)resp.status);
        }

        try {
            auto obj = json::parse(resp.body);
            auto code = obj["code"].get<int>();
            auto data = obj["data"].get<std::string>();
            return code == 200 && data == "Pong";
        }
        catch (const std::exception& e) {
            LOGE("Ping Exception: {}, body: {}", e.what(), resp.body);
            return TRError(SpvrApiError::kParseJsonFailed);
        }
    }

    Result<SpvrDevicePtr, SpvrApiError> SpvrDeviceApi::RequestNewDevice(const std::string& host,
                                                                  int port,
                                                                  const std::string& appkey,
                                                                  const std::string& default_name,
                                                                  const std::string& info) {
        std::string hw_info;
        if (info.empty()) {
            auto hardware_desc = Hardware::Instance()->GetHardwareDescription();
            auto et_info = IPUtil::ScanIPs();
            std::string mac_address;
            for (auto &item: et_info) {
                if (!item.mac_address_.empty() && mac_address.find(item.mac_address_) != std::string::npos) {
                    continue;
                }
                mac_address = mac_address.append(item.mac_address_);
            }
            if (hardware_desc.empty()) {
                LOGW("Hardware desc is empty! Can't request new device!");
            }
            hw_info = Base64::Base64Encode(hardware_desc + mac_address);
        }
        else {
            hw_info = info;
        }

        // SHIT!
        if (hw_info.empty()) {
            hw_info = GetUUID();
        }

        auto client = HttpClient::MakeSSL(host, port, kApiRequestNewDevice);
        auto resp = client->Post({
#ifdef WIN32
            {"platform", "windows"},
#else
            {"platform", "android"},
#endif
             {"hw_info", hw_info},
             {"appkey", appkey},
             {"device_name", default_name}
        });

        LOGI("RequestNewDevice, hw_info: {}, appkey: {}", hw_info, appkey);
        LOGI("NewDeviceResp, status: {}, body: {}", resp.status, resp.body);
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("Request new device failed, code: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = SpvrDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(SpvrApiError::kParseJsonFailed);

        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    Result<SpvrDevicePtr, SpvrApiError> SpvrDeviceApi::UpdateRandomPwd(const std::string& host,
                                                                 int port,
                                                                 const std::string& appkey,
                                                                 const std::string& target_device_id) {
        auto client = HttpClient::MakeSSL(host, port, kApiUpdateRandomPwd);
        auto resp = client->Post({
            {"device_id", target_device_id},
            {"appkey", appkey}
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("UpdateRandomPwd failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = SpvrDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    Result<SpvrDevicePtr, SpvrApiError> SpvrDeviceApi::UpdateSafetyPwd(const std::string& host,
                                                                 int port,
                                                                 const std::string& appkey,
                                                                 const std::string& target_device_id,
                                                                 const std::string& safety_pwd_md5) {
        auto client = HttpClient::MakeSSL(host, port, kApiUpdateSafetyPwd, 2000);
        auto resp = client->Post({
            {"device_id", target_device_id},
            {"safety_pwd_md5", safety_pwd_md5},
            {"appkey", appkey}
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("UpdateSafetyPwd failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = SpvrDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    Result<SpvrDevicePtr, SpvrApiError> SpvrDeviceApi::QueryDevice(const std::string& host,
                                                             int port,
                                                             const std::string& appkey,
                                                             const std::string& device_id) {
        auto client = HttpClient::MakeSSL(host, port, kApiQueryDeviceById);
        auto resp = client->Request({
            {"device_id", device_id},
            {"appkey", appkey}
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("GetDevice failed: {}, code: {}", device_id, resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = SpvrDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}, resp.body: {}", e.what(), resp.body);
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    bool SpvrDeviceApi::IsDeviceOnline(const std::string& host,
                                       int port,
                                       const std::string& appkey,
                                       const std::string& device_id) {
        auto client = HttpClient::MakeSSL(host, port, kApiQueryPanelConnByDeviceId);
        auto resp = client->Request({
            {"device_id", device_id},
            {"appkey", appkey}
        });
        // 200: the device holds a live panel connection to the spvr server right now.
        // Otherwise (400/DeviceNotFound, network error, ...): offline.
        return resp.status == 200 && !resp.body.empty();
    }

    tc::Result<SpvrDevicePtr, SpvrApiError> SpvrDeviceApi::UpdateDesktopLink(const std::string& host,
                                                                             int port,
                                                                             const std::string& appkey,
                                                                             const std::string& device_id,
                                                                             const std::string& desktop_link,
                                                                             const std::string& desktop_link_raw) {
        auto client = HttpClient::MakeSSL(host, port, kApiUpdateDesktopLink, 2000);
        json obj;
        obj[kDeviceId] = device_id;
        obj[kDeviceDesktopLink] = desktop_link;
        obj[kDeviceDesktopLinkRaw] = desktop_link_raw;
        auto resp = client->Post({
            {"appkey", appkey}
        }, obj.dump());

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("UpdateDesktopLink failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = SpvrDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    tc::Result<SpvrDevicePtr, SpvrApiError> SpvrDeviceApi::UpdateDeviceName(const std::string& host,
                                                                            int port,
                                                                            const std::string& appkey,
                                                                            const std::string& device_id,
                                                                            const std::string& device_name) {
        auto client = HttpClient::MakeSSL(host, port, kApiUpdateDeviceName, 2000);
        json obj;
        obj[kDeviceId] = device_id;
        obj[kDeviceName] = device_name;
        auto resp = client->Post({
            {"appkey", appkey}
        }, obj.dump());

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("UpdateDeviceName failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = SpvrDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    tc::Result<SpvrDevicePtr, SpvrApiError> SpvrDeviceApi::UpdateUsedTime(const std::string& host,
                                                                          int port,
                                                                          const std::string& appkey,
                                                                          const std::string& device_id,
                                                                          int period) {
        auto client = HttpClient::MakeSSL(host, port, kApiAppendUsedTime, 2000);
        json obj;
        obj[kDeviceId] = device_id;
        obj["period"] = period;
        auto resp = client->Post({
            {"appkey", appkey},
            {"period", std::to_string(period)},
            {kDeviceId, device_id},
        }, obj.dump());

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("UpdateDeviceName failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = SpvrDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

}
