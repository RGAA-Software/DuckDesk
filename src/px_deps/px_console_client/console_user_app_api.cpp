#include "console_user_app_api.h"

#include <format>
#include <map>
#include <nlohmann/json.hpp>
#include <string_view>

#include "console_api.h"
#include "console_http_client.h"
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

ConsoleUserAppInstance ParseInstance(const json& payload) {
    const auto state = payload.value("state", "");
    return {.instance_id = payload.value("id", ""),
            .app_id = payload.value("application_id", ""),
            .state = state,
            .error_code = {},
            .reconnectable = state == "running",
            .revision = payload.value("revision", 0LL)};
}

px::Result<json, ConsoleApiError> QueryInstance(const std::string& host, const int port, const std::string& access_token,
                                                const std::string& instance_id, const bool guest) {
    const auto client = MakeConsoleHttpClient(host, port, std::format("/api/console/instances/{}", instance_id), 5'000);
    SetPanelRequestHeaders(client, access_token, guest ? "guest" : "user");
    const auto response = client->Request();
    if (response.status != 200 || response.body.empty()) {
        return HttpError<json>("QueryInstance", response);
    }
    try {
        return json::parse(response.body);
    } catch (const std::exception& error) {
        LOGE("QueryInstance response parsing failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<std::vector<json>, ConsoleApiError> QueryPages(const std::string& host, const int port, const std::string& path,
                                                          const std::string& access_token, const bool guest, const bool resource_request) {
    std::vector<json> rows{};
    std::string after{};
    for (int page{}; page < 100; ++page) {
        const auto client = MakeConsoleHttpClient(host, port, path, 5'000);
        SetPanelRequestHeaders(client, access_token, resource_request ? (guest ? "guest" : "user") : "");
        std::map<std::string, std::string> query{{"limit", "100"}};
        if (!after.empty()) {
            query.emplace("after", after);
        }
        const auto response = client->Request(query);
        if (response.status != 200 || response.body.empty()) {
            return HttpError<std::vector<json>>("QueryPages", response);
        }
        try {
            const auto payload = json::parse(response.body);
            if (!payload.is_array()) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            for (const auto& row : payload) {
                rows.push_back(row);
            }
            if (payload.size() < 100) {
                return rows;
            }
            after = payload.back().value("id", "");
            if (after.empty()) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
        } catch (const std::exception& error) {
            LOGE("Paged Console response parsing failed: {}", error.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }
    return TcErr(ConsoleApiError::kInternalError);
}

}  // namespace

px::Result<std::vector<ConsoleUserAppInstance>, ConsoleApiError> ConsoleUserAppApi::QueryInstances(const std::string& host, const int port,
                                                                                                   const std::string& access_token,
                                                                                                   const bool guest) {
    const auto rows = QueryPages(host, port, "/api/console/instances", access_token, guest, true);
    if (!rows) {
        return TcErr(rows.error());
    }
    std::vector<ConsoleUserAppInstance> instances{};
    for (const auto& row : rows.value()) {
        auto instance = ParseInstance(row);
        if (instance.instance_id.empty() || instance.app_id.empty() || instance.revision <= 0) {
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
        instances.push_back(std::move(instance));
    }
    return instances;
}

px::Result<std::string, ConsoleApiError> ConsoleUserAppApi::CreateGuestSession(const std::string& host, const int port,
                                                                               const std::string& /*client_nonce*/) {
    const auto client = MakeConsoleHttpClient(host, port, "/api/console/guest-sessions", 3'000);
    SetPanelRequestHeaders(client);
    const auto response = client->Post({}, "{}", "application/json");
    if (response.status != 201 || response.body.empty()) {
        return HttpError<std::string>("CreateGuestSession", response);
    }
    try {
        const auto token = json::parse(response.body).value("token", "");
        return token.empty() ? px::Result<std::string, ConsoleApiError>{TcErr(ConsoleApiError::kParseJsonFailed)}
                             : px::Result<std::string, ConsoleApiError>{token};
    } catch (const std::exception& error) {
        LOGE("CreateGuestSession response parsing failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<std::vector<ConsoleUserApplication>, ConsoleApiError> ConsoleUserAppApi::QueryApps(const std::string& host, const int port,
                                                                                              const std::string& access_token, const bool guest) {
    const auto path = guest ? "/api/console/guest/applications" : "/api/console/applications";
    const auto rows = QueryPages(host, port, path, access_token, guest, false);
    if (!rows) {
        return TcErr(rows.error());
    }
    std::vector<ConsoleUserApplication> applications{};
    for (const auto& application_row : rows.value()) {
        ConsoleUserApplication application{.app_id = application_row.value("id", ""),
                                           .app_type = application_row.value("kind", ""),
                                           .name = application_row.value("name", ""),
                                           .access_mode = application_row.value("access_mode", ""),
                                           .cover_url = {},
                                           .version = application_row.value("revision", 0LL)};
        if (application.app_id.empty() || application.name.empty() || application.app_type.empty() || application.version <= 0) {
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
        applications.push_back(std::move(application));
    }
    return applications;
}

px::Result<ConsoleUserAppInstance, ConsoleApiError> ConsoleUserAppApi::StartApp(const std::string& host, const int port,
                                                                                const std::string& access_token, const std::string& app_id,
                                                                                const std::string& client_nonce, const bool guest) {
    const auto client = MakeConsoleHttpClient(host, port, "/api/console/instances", 30'000);
    SetPanelRequestHeaders(client, access_token, guest ? "guest" : "user");
    const auto response =
        client->Post({}, json{{"request_id", client_nonce}, {"application_id", app_id}, {"deployment_id", nullptr}}.dump(), "application/json");
    if (response.status != 201 || response.body.empty()) {
        return HttpError<ConsoleUserAppInstance>("StartApp", response);
    }
    try {
        auto result = ParseInstance(json::parse(response.body));
        return result.instance_id.empty() || result.app_id != app_id || result.revision <= 0
                   ? px::Result<ConsoleUserAppInstance, ConsoleApiError>{TcErr(ConsoleApiError::kParseJsonFailed)}
                   : px::Result<ConsoleUserAppInstance, ConsoleApiError>{std::move(result)};
    } catch (const std::exception& error) {
        LOGE("StartApp response parsing failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<ConsoleNativeApplicationConnection, ConsoleApiError> ConsoleUserAppApi::QueryNativeConnection(
    const std::string& host, const int port, const std::string& access_token, const std::string& instance_id, const bool view_only,
    const std::string& request_id, const bool guest) {
    const auto instance = QueryInstance(host, port, access_token, instance_id, guest);
    if (!instance) {
        return TcErr(instance.error());
    }
    const auto application_id = instance->value("application_id", "");
    if (application_id.empty()) {
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
    const auto resource = OpenPanelResourceConnection(
        host, port, access_token, guest,
        {.kind = ConsoleResourceTargetKind::CloudApplication, .application_id = application_id, .instance_id = instance_id}, view_only, request_id);
    if (!resource) {
        return TcErr(resource.error());
    }
    return ConsoleNativeApplicationConnection{.host = resource->host,
                                              .port = resource->port,
                                              .device_id = resource->remote_resource_id,
                                              .instance_id = instance_id,
                                              .app_type = {},
                                              .session_id = resource->session_id,
                                              .session_revision = resource->session_revision,
                                              .frontend_token = resource->frontend_token,
                                              .relay_host = resource->relay_host,
                                              .relay_port = resource->relay_port,
                                              .relay_admission_ticket = resource->relay_admission_ticket};
}

px::Result<ConsoleUserAppInstance, ConsoleApiError> ConsoleUserAppApi::StopInstance(const std::string& host, const int port,
                                                                                    const std::string& access_token, const std::string& instance_id,
                                                                                    const bool guest) {
    const auto current = QueryInstance(host, port, access_token, instance_id, guest);
    if (!current) {
        return TcErr(current.error());
    }
    const auto revision = current->value("revision", 0LL);
    const auto client = MakeConsoleHttpClient(host, port, std::format("/api/console/instances/{}/stop", instance_id), 5'000);
    SetPanelRequestHeaders(client, access_token, guest ? "guest" : "user");
    const auto response = client->Post({}, json{{"revision", revision}}.dump(), "application/json");
    if (response.status != 200 || response.body.empty()) {
        return HttpError<ConsoleUserAppInstance>("StopInstance", response);
    }
    try {
        auto result = ParseInstance(json::parse(response.body));
        return result.instance_id == instance_id ? px::Result<ConsoleUserAppInstance, ConsoleApiError>{std::move(result)}
                                                 : px::Result<ConsoleUserAppInstance, ConsoleApiError>{TcErr(ConsoleApiError::kParseJsonFailed)};
    } catch (const std::exception& error) {
        LOGE("StopInstance response parsing failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

}  // namespace px_console
