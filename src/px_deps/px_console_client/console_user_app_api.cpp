#include "console_user_app_api.h"

#include <algorithm>
#include <format>
#include <nlohmann/json.hpp>
#include <string_view>

#include "console_http_client.h"
#include "console_api.h"
#include "console_user.h"
#include "px_common_new/http_client.h"
#include "px_common_new/log.h"

using nlohmann::json;
using namespace px;

namespace px_console {
namespace {

constexpr auto kResponseData = "data";

ConsoleUserAppInstance ParseInstance(const json& data) {
    ConsoleUserAppInstance instance;
    instance.instance_id = data.value("instance_id", "");
    instance.state = data.value("state", "");
    // A successful instance has no error code. Console represents that state as
    // JSON null, while nlohmann::json::value() only applies its default when
    // the key is absent and throws when the key exists with a null value.
    // Treat both a missing key and null as the expected empty error code.
    if (const auto error_code = data.find("error_code");
        error_code != data.end() && !error_code->is_null()) {
        instance.error_code = error_code->get<std::string>();
    }
    instance.reconnectable = data.value("reconnectable", false);
    return instance;
}

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

px::Result<std::string, ConsoleApiError>
ConsoleUserAppApi::CreateGuestSession(const std::string& host, int port,
                                  const std::string& client_nonce) {
    const auto client = MakeConsoleHttpClient(host, port, "/api/v1/session/guest", 3000);
    const auto response = client->Post({}, json{{"client_nonce", client_nonce},
        {"client_type", "panel"}}.dump(), "application/json");
    if (response.status != 200 || response.body.empty()) {
        return HttpError<std::string>("CreateGuestSession", response);
    }
    try {
        const auto token = json::parse(response.body).at(kResponseData).value("access_token", "");
        if (token.empty()) return TcErr(ConsoleApiError::kParseJsonFailed);
        return token;
    } catch (const std::exception& error) {
        LOGE("CreateGuestSession parse failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<std::vector<ConsoleUserApplication>, ConsoleApiError>
ConsoleUserAppApi::QueryApps(const std::string& host, int port, const std::string& access_token,
                         bool guest) {
    const auto client = MakeConsoleHttpClient(host, port,
        guest ? "/api/v1/public/apps" : "/api/v1/user/apps", 3000);
    client->SetHeader("Authorization", "Bearer " + access_token);
    const auto response = client->Request();
    if (response.status != 200 || response.body.empty()) {
        return HttpError<std::vector<ConsoleUserApplication>>("QueryApps", response);
    }
    try {
        const auto data = json::parse(response.body).at(kResponseData);
        std::vector<ConsoleUserApplication> apps;
        for (const auto& item : data) {
            ConsoleUserApplication app;
            app.app_id = item.value("app_id", "");
            app.name = item.value("name", "");
            app.access_mode = item.value("access_mode", "public");
            app.cover_url = item.value("cover_url", "");
            app.version = item.value("version", 0LL);
            if (item.contains("running_instance") && !item["running_instance"].is_null()) {
                app.running_instance = std::make_shared<ConsoleUserAppInstance>(ParseInstance(item["running_instance"]));
            }
            if (!app.app_id.empty()) apps.push_back(std::move(app));
        }
        return apps;
    } catch (const std::exception& error) {
        LOGE("QueryApps parse failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<ConsoleUserAppInstance, ConsoleApiError>
ConsoleUserAppApi::StartApp(const std::string& host, int port, const std::string& access_token,
                        const std::string& app_id, const std::string& client_nonce, bool guest) {
    const auto path = guest
        ? std::format("/api/v1/public/apps/{}/start", app_id)
        : std::format("/api/v1/user/apps/{}/start", app_id);
    const auto client = MakeConsoleHttpClient(host, port, path, 30000);
    client->SetHeader("Authorization", "Bearer " + access_token);
    const auto response = client->Post({}, json{{"client_nonce", client_nonce}}.dump(), "application/json");
    // Console returns 200 when an idempotent start reuses an instance and 202
    // while a newly scheduled instance is starting. Both are successful.
    if ((response.status != 200 && response.status != 202) || response.body.empty()) {
        return HttpError<ConsoleUserAppInstance>("StartApp", response);
    }
    try {
        return ParseInstance(json::parse(response.body).at(kResponseData));
    } catch (const std::exception& error) {
        LOGE("StartApp parse failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<ConsoleConnectionTicket, ConsoleApiError>
ConsoleUserAppApi::IssueInstanceTicket(const std::string& host, int port,
                                   const std::string& access_token,
                                   const std::string& instance_id,
                                   const std::string& client_nonce,
                                   const std::vector<std::string>& requested_permissions,
                                   bool guest) {
    const auto path = guest
        ? std::format("/api/v1/public/instances/{}/ticket", instance_id)
        : std::format("/api/v1/user/instances/{}/ticket", instance_id);
    const auto client = MakeConsoleHttpClient(host, port, path, 3000);
    client->SetHeader("Authorization", "Bearer " + access_token);
    const auto response = client->Post({}, json{{"client_nonce", client_nonce},
        {"join_mode", std::find(requested_permissions.begin(), requested_permissions.end(), "input")
            == requested_permissions.end() ? "observe" : "control"}}.dump(), "application/json");
    if (response.status != 200 || response.body.empty()) {
        return HttpError<ConsoleConnectionTicket>("IssueInstanceTicket", response);
    }
    try {
        const auto data = json::parse(response.body).at(kResponseData);
        ConsoleConnectionTicket ticket;
        ticket.ticket = data.value("ticket", "");
        ticket.renewal_token = data.value("renewal_token", "");
        ticket.launch_url = data.value("launch_url", "");
        ticket.expires_at = data.value("expires_at", 0LL);
        ticket.logical_session_id = data.value("logical_session_id", "");
        ticket.stream_id = data.value("stream_id", "");
        ticket.join_mode = data.value("join_mode", "control");
        ticket.permissions = data.value("permissions", std::vector<std::string>{});
        ticket.rtc_ice_config_json = data.contains("rtc_ice_config")
            ? data.at("rtc_ice_config").dump() : "";
        ticket.relay_host = data.value("relay_host", "");
        ticket.relay_port = data.value("relay_port", 0);
        ticket.signal_device_id = data.value("signal_device_id", "");
        if (ticket.ticket.empty() || ticket.renewal_token.empty()
            || ticket.stream_id.empty() || ticket.launch_url.empty()) {
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
        return ticket;
    } catch (const std::exception& error) {
        LOGE("IssueInstanceTicket parse failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<ConsoleUserAppInstance, ConsoleApiError>
ConsoleUserAppApi::StopInstance(const std::string& host, int port, const std::string& access_token,
                            const std::string& instance_id, bool guest) {
    const auto path = guest
        ? std::format("/api/v1/public/instances/{}/stop", instance_id)
        : std::format("/api/v1/user/instances/{}/stop", instance_id);
    const auto client = MakeConsoleHttpClient(host, port, path, 5000);
    client->SetHeader("Authorization", "Bearer " + access_token);
    const auto response = client->Post({}, "{}", "application/json");
    if (response.status != 200 || response.body.empty()) {
        return HttpError<ConsoleUserAppInstance>("StopInstance", response);
    }
    try {
        return ParseInstance(json::parse(response.body).at(kResponseData));
    } catch (const std::exception& error) {
        LOGE("StopInstance parse failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

}
