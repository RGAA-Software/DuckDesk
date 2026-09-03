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
#include <algorithm>
#include <format>
#include <nlohmann/json.hpp>
#include <string_view>

const std::string kQueryUserDevices = "/api/v1/user/devices";

using namespace px;
using namespace nlohmann;

namespace px_console
{

    namespace {

        constexpr std::string_view kRenewConnectionTicket = "/api/v1/connection-tickets/renew";

        template <typename T>
        px::Result<T, ConsoleApiError> HttpError(std::string_view operation,
                                                const px::HttpResponse& response) {
            const auto error = ToConsoleUserApiError(response);
            const auto message = ConsoleApiLastErrorMessage();
            LOGE("{} failed: HTTP {}, transport: {}, message: {}", operation, response.status,
                 response.error_code, message.empty() ? "<empty>" : message);
            return TcErr(error);
        }

        px::Result<ConsoleConnectionTicket, ConsoleApiError> ParseConnectionTicket(
            const nlohmann::json& data, const bool require_launch_url) {
            ConsoleConnectionTicket result;
            result.ticket = data.value("ticket", "");
            result.renewal_token = data.value("renewal_token", "");
            result.launch_url = data.value("launch_url", "");
            result.expires_at = data.value("expires_at", 0LL);
            result.logical_session_id = data.value("logical_session_id", "");
            result.stream_id = data.value("stream_id", "");
            result.join_mode = data.value("join_mode", "control");
            result.permissions = data.value("permissions", std::vector<std::string>{});
            result.rtc_ice_config_json = data.contains("rtc_ice_config")
                ? data.at("rtc_ice_config").dump() : "";
            result.relay_host = data.value("relay_host", "");
            result.relay_port = data.value("relay_port", 0);
            result.signal_device_id = data.value("signal_device_id", "");
            if (result.ticket.empty() || result.renewal_token.empty() || result.stream_id.empty()
                || (require_launch_url && result.launch_url.empty())) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            return result;
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
        // The server owns capability assignment. The legacy vector remains
        // only as a UI-intent bridge until callers move to an explicit role.
        obj["join_mode"] = std::find(
            requested_permissions.begin(), requested_permissions.end(), "input")
            == requested_permissions.end() ? "observe" : "control";
        auto resp = client->Post({}, obj.dump(), "application/json");

        if (resp.status != 200 || resp.body.empty()) {
            return HttpError<ConsoleConnectionTicket>("IssueDeviceTicket", resp);
        }

        try {
            auto data = json::parse(resp.body)[kData];
            return ParseConnectionTicket(data, true);
        }
        catch (const std::exception& e) {
            LOGE("IssueDeviceTicket parse failed: {}", e.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

    px::Result<ConsoleConnectionTicket, ConsoleApiError>
    ConsoleUserDeviceApi::RenewConnectionTicket(const std::string& host,
                                                  const int port,
                                                  const std::string& renewal_token,
                                                  const std::string& client_nonce) {
        const auto client = MakeConsoleHttpClient(host, port, std::string(kRenewConnectionTicket), 3000);
        const auto response = client->Post({}, json{
            {"renewal_token", renewal_token},
            {"client_nonce", client_nonce},
        }.dump(), "application/json");
        if (response.status != 200 || response.body.empty()) {
            return HttpError<ConsoleConnectionTicket>("RenewConnectionTicket", response);
        }
        try {
            return ParseConnectionTicket(json::parse(response.body).at(kData), false);
        }
        catch (const std::exception& error) {
            LOGE("RenewConnectionTicket parse failed: {}", error.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }
}
