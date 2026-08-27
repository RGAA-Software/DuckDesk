//
// Created by RGAA on 28/11/2025.
//

#include "console_user_device_api.h"
#include "console_http_client.h"
#include "px_common_new/log.h"
#include "px_common_new/http_client.h"
#include "px_common_new/http_base_op.h"
#include "console_user.h"
#include "console_user_device.h"
#include "console_device.h"
#include "console_api.h"
#include <format>
#include <nlohmann/json.hpp>
#include <string_view>

const std::string kQueryUserDevices = "/api/v1/user/devices";

using namespace px;
using namespace nlohmann;

namespace px_console
{

    namespace {

        template <typename T>
        px::Result<T, ConsoleApiError> HttpError(std::string_view operation,
                                                const px::HttpResponse& response) {
            const auto error = ToConsoleUserApiError(response);
            const auto message = ConsoleApiLastErrorMessage();
            LOGE("{} failed: HTTP {}, transport: {}, message: {}", operation, response.status,
                 response.error_code, message.empty() ? "<empty>" : message);
            return TcErr(error);
        }

    }

    px::Result<std::vector<std::shared_ptr<ConsoleUserDevice>>, ConsoleApiError>
    ConsoleUserDeviceApi::QueryUserBindDevices(const std::string& host,
                                            int port,
                                            const std::string& access_token) {
        const auto client = MakeConsoleHttpClient(host, port, kQueryUserDevices, 2000);
        client->SetHeader("Authorization", "Bearer " + access_token);
        auto resp = client->Request();

        if (resp.status != 200 || resp.body.empty()) {
            return HttpError<std::vector<std::shared_ptr<ConsoleUserDevice>>>(
                "QueryUserDevices", resp);
        }

        try {
            json obj = json::parse(resp.body);
            auto body_array = obj[kData];
            if (!body_array.is_array()) {
                LOGE("QueryUserBindDevices invalid data: {}", resp.body);
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }

            std::vector<std::shared_ptr<ConsoleUserDevice>> devices;
            for (const auto& item : body_array) {
                if (auto r = ConsoleUserDevice::FromObj(item); r) {
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
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

    px::Result<ConsoleConnectionTicket, ConsoleApiError>
    ConsoleUserDeviceApi::IssueDeviceTicket(const std::string& host,
                                        int port,
                                        const std::string& access_token,
                                        const std::string& device_id,
                                        const std::string& client_nonce,
                                        const std::vector<std::string>& requested_permissions) {
        const auto path = std::format("/api/v1/user/devices/{}/ticket", device_id);
        const auto client = MakeConsoleHttpClient(host, port, path, 3000);
        client->SetHeader("Authorization", "Bearer " + access_token);
        json obj;
        obj["client_nonce"] = client_nonce;
        obj["requested_permissions"] = requested_permissions;
        auto resp = client->Post({}, obj.dump(), "application/json");

        if (resp.status != 200 || resp.body.empty()) {
            return HttpError<ConsoleConnectionTicket>("IssueDeviceTicket", resp);
        }

        try {
            auto data = json::parse(resp.body)[kData];
            ConsoleConnectionTicket result;
            result.ticket = data.value("ticket", "");
            result.launch_url = data.value("launch_url", "");
            result.expires_at = data.value("expires_at", 0LL);
            result.permissions = data.value("permissions", std::vector<std::string>{});
            result.rtc_ice_config_json = data.contains("rtc_ice_config")
                ? data.at("rtc_ice_config").dump() : "";
            result.relay_host = data.value("relay_host", "");
            result.relay_port = data.value("relay_port", 0);
            if (result.ticket.empty() || result.launch_url.empty()) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            return result;
        }
        catch (const std::exception& e) {
            LOGE("IssueDeviceTicket parse failed: {}", e.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }
}
