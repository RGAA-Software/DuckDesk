#include "console_user_device_api.h"

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <string_view>

#include "console_api.h"
#include "console_http_client.h"
#include "console_resource_api.h"
#include "console_user_device.h"
#include "px_common/http_client.h"
#include "px_common/log.h"
#include "px_common/uuid.h"

namespace px_console {
namespace {

using nlohmann::json;

template <typename Value>
px::Result<Value, ConsoleApiError> HttpError(const std::string_view operation, const px::HttpResponse& response) {
    const auto error = ToConsoleUserApiError(response);
    const auto message = ConsoleApiLastErrorMessage();
    LOGE("{} failed: HTTP {}, transport: {}, message: {}", operation, response.status, response.error_code, message.empty() ? "<empty>" : message);
    return TcErr(error);
}

}  // namespace

px::Result<ConsolePublicDeviceEndpoint, ConsoleApiError> ConsoleUserDeviceApi::ResolvePublicCode(const std::string& host, const int port,
                                                                                                 const std::string& public_code) {
    if (public_code.size() != 9 ||
        !std::ranges::all_of(public_code, [](const char digit) { return digit >= '0' && digit <= '9'; })) {
        return TcErr(ConsoleApiError::kInvalidParams);
    }
    const auto client = MakeConsoleHttpClient(host, port, "/api/console/public/devices/" + public_code, 3'000);
    SetPanelRequestHeaders(client);
    const auto response = client->Request();
    if (response.status != 200 || response.body.empty()) {
        return HttpError<ConsolePublicDeviceEndpoint>("ResolvePublicCode", response);
    }
    try {
        const auto payload = json::parse(response.body);
        ConsolePublicDeviceEndpoint endpoint{.device_id = payload.at("device_id").get<std::string>(),
                                             .public_code = payload.at("public_code").get<std::string>(),
                                             .name = payload.at("name").get<std::string>(),
                                             .host = payload.at("host").get<std::string>(),
                                             .port = payload.at("port").get<int>()};
        if (endpoint.device_id.empty() || endpoint.public_code != public_code || endpoint.name.empty() || endpoint.host.empty() ||
            endpoint.port <= 0 || endpoint.port > 65535) {
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
        return endpoint;
    } catch (const std::exception&) {
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<std::vector<std::shared_ptr<ConsoleUserDevice>>, ConsoleApiError> ConsoleUserDeviceApi::QueryUserBindDevices(
    const std::string& host, const int port, const std::string& access_token) {
    std::vector<std::shared_ptr<ConsoleUserDevice>> devices{};
    std::string after{};
    for (int page{}; page < 100; ++page) {
        const auto client = MakeConsoleHttpClient(host, port, "/api/console/devices", 3'000);
        SetPanelRequestHeaders(client, access_token);
        std::map<std::string, std::string> query{{"limit", "100"}};
        if (!after.empty()) {
            query.emplace("after", after);
        }
        const auto response = client->Request(query);
        if (response.status != 200 || response.body.empty()) {
            return HttpError<std::vector<std::shared_ptr<ConsoleUserDevice>>>("QueryUserDevices", response);
        }
        try {
            const auto payload = json::parse(response.body);
            if (!payload.is_array()) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            for (const auto& device_row : payload) {
                auto device = ConsoleUserDevice::FromObj(device_row);
                if (!device) {
                    return TcErr(ConsoleApiError::kParseJsonFailed);
                }
                devices.push_back(std::move(device));
            }
            if (payload.size() < 100) {
                return devices;
            }
            after = payload.back().value("id", "");
            if (after.empty()) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
        } catch (const std::exception& error) {
            LOGE("QueryUserDevices response parsing failed: {}", error.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }
    return TcErr(ConsoleApiError::kInternalError);
}

px::Result<ConsoleNativeDeviceConnection, ConsoleApiError> ConsoleUserDeviceApi::QueryNativeConnection(const std::string& host, const int port,
                                                                                                       const std::string& access_token,
                                                                                                       const std::string& device_id,
                                                                                                       const bool view_only) {
    const auto resource = OpenPanelResourceConnection(
        host, port, access_token, false, {.kind = ConsoleResourceTargetKind::Desktop, .device_id = device_id}, view_only, px::GetCanonicalUUID());
    if (!resource) {
        return TcErr(resource.error());
    }
    return ConsoleNativeDeviceConnection{.host = resource->host,
                                         .port = resource->port,
                                         .device_id = device_id,
                                         .session_id = resource->session_id,
                                         .session_revision = resource->session_revision,
                                         .frontend_token = resource->frontend_token,
                                         .relay_host = resource->relay_host,
                                         .relay_port = resource->relay_port,
                                         .relay_admission_ticket = resource->relay_admission_ticket};
}

}  // namespace px_console
