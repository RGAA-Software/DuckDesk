//
// Created by RGAA on 28/11/2025.
//

#include "cms_user_device_api.h"
#include "px_common_new/log.h"
#include "px_common_new/http_client.h"
#include "px_common_new/http_base_op.h"
#include "cms_user.h"
#include "cms_user_device.h"
#include "cms_device.h"
#include <nlohmann/json.hpp>

const std::string kUserDeviceControl = "/api/v1/user_device/control";
const std::string kQueryUserDevices = kUserDeviceControl + "/query/user/devices";
const std::string kAddDeviceForUser = kUserDeviceControl + "/add/device/for/user";
const std::string kRemoveDeviceFromUser = kUserDeviceControl + "/remove/device/from/user";

using namespace px;
using namespace nlohmann;

namespace px_cms
{

    px::Result<std::vector<std::shared_ptr<CmsUserDevice>>, CmsApiError>
    CmsUserDeviceApi::QueryUserBindDevices(const std::string& host,
                                            int port,
                                            const std::string& appkey,
                                            const std::string& uid,
                                            int page,
                                            int page_size) {
        const auto client = HttpClient::MakeSSL(host, port, kQueryUserDevices, 2000);
        auto resp = client->Request({
            {"appkey", appkey},
            {kUserId, uid},
            {kPage, std::to_string(page)},
            {kPageSize, std::to_string(page_size)},
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("QueryUserDevices failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            json obj = json::parse(resp.body);
            auto body_array = obj[kData];
            if (!body_array.is_array()) {
                LOGE("QueryUserBindDevices invalid data: {}", resp.body);
                return TcErr(CmsApiError::kParseJsonFailed);
            }

            std::vector<std::shared_ptr<CmsUserDevice>> devices;
            for (const auto& item : body_array) {
                if (auto r = CmsUserDevice::FromObj(item); r) {
                    devices.push_back(r);
                }
                else {
                    LOGE("QueryUserBindDevices parse item failed: {}", item.dump());
                    continue;
                }
            }
            return devices;
        }
        catch (const std::exception& e) {
            LOGE("QueryUserBindDevices parse failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    px::Result<std::shared_ptr<CmsUserDevice>, CmsApiError>
    CmsUserDeviceApi::AddDeviceForUser(const std::string& host,
                                        int port,
                                        const std::string& appkey,
                                        const std::string& uid,
                                        const std::string& device_id) {
        const auto client = HttpClient::MakeSSL(host, port, kAddDeviceForUser, 2000);
        json obj;
        obj[kUserId] = uid;
        obj[kDeviceId] = device_id;
        auto resp = client->Post({
            {"appkey", appkey},
        }, obj.dump());

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("QueryUserDevices failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            json obj = json::parse(resp.body)[kData];
            auto r = CmsUserDevice::FromObj(obj);
            return r;
        }
        catch (const std::exception& e) {
            LOGE("QueryUserBindDevices parse failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    px::Result<std::shared_ptr<CmsUserDevice>, CmsApiError>
    CmsUserDeviceApi::RemoveDeviceFromUser(const std::string& host,
                                            int port,
                                            const std::string& appkey,
                                            const std::string& uid,
                                            const std::string& device_id) {
        const auto client = HttpClient::MakeSSL(host, port, kRemoveDeviceFromUser, 2000);
        json obj;
        obj[kUserId] = uid;
        obj[kDeviceId] = device_id;
        auto resp = client->Post({
            {"appkey", appkey},
        }, obj.dump());

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("QueryUserDevices failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            json obj = json::parse(resp.body)[kData];
            auto r = CmsUserDevice::FromObj(obj);
            return r;
        }
        catch (const std::exception& e) {
            LOGE("QueryUserBindDevices parse failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }
}