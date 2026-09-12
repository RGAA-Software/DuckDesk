//
// Created by RGAA on 28/11/2025.
//

#include "console_user_device_api.h"
#include "console_http_client.h"
#include "px_common/log.h"
#include "px_common/http_client.h"
#include "px_common/http_base_op.h"
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

    px::Result<ConsoleNativeDeviceConnection, ConsoleApiError>
    ConsoleUserDeviceApi::QueryNativeConnection(const std::string& host,
                                                 const int port,
                                                 const std::string& access_token,
                                                 const std::string& device_id) {
        const auto path = std::format("/api/v1/user/devices/{}/native-connection", device_id);
        const auto client = MakeConsoleHttpClient(host, port, path, 3000);
        client->SetHeader("Authorization", "Bearer " + access_token);
        const auto response = client->Post({}, "{}", "application/json");
        if (response.status != 200 || response.body.empty()) {
            return HttpError<ConsoleNativeDeviceConnection>("QueryNativeDeviceConnection", response);
        }
        try {
            const auto data = json::parse(response.body).at(kData);
            ConsoleNativeDeviceConnection result{.host = data.value("host", ""),
                                                 .port = data.value("port", 0),
                                                 .device_id = data.value("device_id", ""),
                                                 .password_hash = data.value("password_hash", ""),
                                                 .signal_device_id = data.value("signal_device_id", ""),
                                                 .relay_host = data.value("relay_host", ""),
                                                 .relay_port = data.value("relay_port", 0)};
            if (result.host.empty() || result.port <= 0 || result.port > 65535 || result.device_id.empty()) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            return result;
        } catch (const std::exception& error) {
            LOGE("QueryNativeDeviceConnection parse failed: {}", error.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

}
