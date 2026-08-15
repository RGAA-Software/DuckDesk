//
// Created by RGAA on 26/03/2025.
//

#include "cms_device_api.h"
#include "cms_server_info.h"
#include "cms_errors.h"
#include <nlohmann/json.hpp>
#include "cms_device.h"
#include "px_common_new/http_client.h"
#include "px_common_new/log.h"
#include "px_common_new/http_base_op.h"
#include "px_common_new/thread.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/hardware.h"
#include "px_common_new/ip_util.h"
#include "px_common_new/base64.h"
#include "px_common_new/uuid.h"

using namespace px;
using namespace nlohmann;

// ping
const std::string kCmsPing = "/ping";

// /api/v1/device/control
const std::string kCmsDeviceControl = "/api/v1/device/control";

// create new device
const std::string kApiRequestNewDevice = kCmsDeviceControl + "/create/new/device";

// update random password
const std::string kApiUpdateRandomPwd = kCmsDeviceControl + "/update/random/pwd";

// update safety password
const std::string kApiUpdateSafetyPwd = kCmsDeviceControl + "/update/safety/pwd";

// get device by id
const std::string kApiQueryDeviceById = kCmsDeviceControl + "/query/device/by/id";

// /api/v1/panel/control
const std::string kCmsPanelControl = "/api/v1/panel/control";

// query the live panel connection of a device (device online check)
const std::string kApiQueryPanelConnByDeviceId = kCmsPanelControl + "/query/panel/conn/by/device/id";

// update desktop link
const std::string kApiUpdateDesktopLink = kCmsDeviceControl + "/update/desktop/link";

// update device name
const std::string kApiUpdateDeviceName = kCmsDeviceControl + "/update/device/name";

// /append/used/time
const std::string kApiAppendUsedTime = kCmsDeviceControl + "/append/used/time";

namespace px_cms
{

    // Ping
    px::Result<bool, CmsApiError> CmsDeviceApi::Ping(const std::string& host, int port, const std::string& appkey) {
        auto client = HttpClient::MakeSSL(host, port, kCmsPing, 3000);
        auto resp = client->Request({
            {"appkey", appkey}
        });
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("GetRelayDeviceInfo failed : {}", resp.status);
            return TRError((CmsApiError)resp.status);
        }

        try {
            auto obj = json::parse(resp.body);
            auto code = obj["code"].get<int>();
            auto data = obj["data"].get<std::string>();
            return code == 200 && data == "Pong";
        }
        catch (const std::exception& e) {
            LOGE("Ping Exception: {}, body: {}", e.what(), resp.body);
            return TRError(CmsApiError::kParseJsonFailed);
        }
    }

    Result<CmsDevicePtr, CmsApiError> CmsDeviceApi::RequestNewDevice(const std::string& host,
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
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = CmsDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(CmsApiError::kParseJsonFailed);

        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    Result<CmsDevicePtr, CmsApiError> CmsDeviceApi::UpdateRandomPwd(const std::string& host,
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
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = CmsDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(CmsApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    Result<CmsDevicePtr, CmsApiError> CmsDeviceApi::UpdateSafetyPwd(const std::string& host,
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
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = CmsDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(CmsApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    Result<CmsDevicePtr, CmsApiError> CmsDeviceApi::QueryDevice(const std::string& host,
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
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = CmsDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(CmsApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}, resp.body: {}", e.what(), resp.body);
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    bool CmsDeviceApi::IsDeviceOnline(const std::string& host,
                                       int port,
                                       const std::string& appkey,
                                       const std::string& device_id) {
        auto client = HttpClient::MakeSSL(host, port, kApiQueryPanelConnByDeviceId);
        auto resp = client->Request({
            {"device_id", device_id},
            {"appkey", appkey}
        });
        // 200: the device holds a live panel connection to the CMS server right now.
        // Otherwise (400/DeviceNotFound, network error, ...): offline.
        return resp.status == 200 && !resp.body.empty();
    }

    px::Result<CmsDevicePtr, CmsApiError> CmsDeviceApi::UpdateDesktopLink(const std::string& host,
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
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = CmsDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(CmsApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    px::Result<CmsDevicePtr, CmsApiError> CmsDeviceApi::UpdateDeviceName(const std::string& host,
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
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = CmsDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(CmsApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    px::Result<CmsDevicePtr, CmsApiError> CmsDeviceApi::UpdateUsedTime(const std::string& host,
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
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            if (auto obj = CmsDevice::FromObj(json_obj); obj) {
                return obj;
            }
            return TcErr(CmsApiError::kParseJsonFailed);
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

}
