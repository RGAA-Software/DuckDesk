//
// Created by RGAA on 28/11/2025.
//

#include "cms_user_device_api.h"
#include "cms_http_client.h"
#include "px_common_new/log.h"
#include "px_common_new/http_client.h"
#include "px_common_new/http_base_op.h"
#include "cms_user.h"
#include "cms_user_device.h"
#include "cms_device.h"
#include <format>
#include <nlohmann/json.hpp>

const std::string kQueryUserDevices = "/api/v1/user/devices";

using namespace px;
using namespace nlohmann;

namespace px_cms
{

    px::Result<std::vector<std::shared_ptr<CmsUserDevice>>, CmsApiError>
    CmsUserDeviceApi::QueryUserBindDevices(const std::string& host,
                                            int port,
                                            const std::string& access_token) {
        const auto client = MakeCmsHttpClient(host, port, kQueryUserDevices, 2000);
        client->SetHeader("Authorization", "Bearer " + access_token);
        auto resp = client->Request();

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

    px::Result<CmsConnectionTicket, CmsApiError>
    CmsUserDeviceApi::IssueDeviceTicket(const std::string& host,
                                        int port,
                                        const std::string& access_token,
                                        const std::string& device_id,
                                        const std::string& client_nonce,
                                        const std::vector<std::string>& requested_permissions) {
        const auto path = std::format("/api/v1/user/devices/{}/ticket", device_id);
        const auto client = MakeCmsHttpClient(host, port, path, 3000);
        client->SetHeader("Authorization", "Bearer " + access_token);
        json obj;
        obj["client_nonce"] = client_nonce;
        obj["requested_permissions"] = requested_permissions;
        auto resp = client->Post({}, obj.dump(), "application/json");

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("IssueDeviceTicket failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto data = json::parse(resp.body)[kData];
            CmsConnectionTicket result;
            result.ticket = data.value("ticket", "");
            result.launch_url = data.value("launch_url", "");
            result.expires_at = data.value("expires_at", 0LL);
            result.permissions = data.value("permissions", std::vector<std::string>{});
            if (result.ticket.empty() || result.launch_url.empty()) {
                return TcErr(CmsApiError::kParseJsonFailed);
            }
            return result;
        }
        catch (const std::exception& e) {
            LOGE("IssueDeviceTicket parse failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }
}
