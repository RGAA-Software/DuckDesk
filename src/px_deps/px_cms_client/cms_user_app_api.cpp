#include "cms_user_app_api.h"

#include <format>
#include <nlohmann/json.hpp>

#include "cms_http_client.h"
#include "cms_user.h"
#include "px_common_new/http_client.h"
#include "px_common_new/log.h"

using nlohmann::json;
using namespace px;

namespace px_cms {
namespace {

constexpr auto kResponseData = "data";

CmsUserAppInstance ParseInstance(const json& data) {
    CmsUserAppInstance instance;
    instance.instance_id = data.value("instance_id", "");
    instance.state = data.value("state", "");
    instance.error_code = data.value("error_code", "");
    instance.reconnectable = data.value("reconnectable", false);
    return instance;
}

template <typename T>
px::Result<T, CmsApiError> HttpError(const char* operation, int status) {
    LOGE("{} failed: HTTP {}", operation, status);
    return TcErr(static_cast<CmsApiError>(status));
}

}

px::Result<std::string, CmsApiError>
CmsUserAppApi::CreateGuestSession(const std::string& host, int port,
                                  const std::string& client_nonce) {
    const auto client = MakeCmsHttpClient(host, port, "/api/v1/session/guest", 3000);
    const auto response = client->Post({}, json{{"client_nonce", client_nonce},
        {"client_type", "panel"}}.dump(), "application/json");
    if (response.status != 200 || response.body.empty()) {
        return HttpError<std::string>("CreateGuestSession", response.status);
    }
    try {
        const auto token = json::parse(response.body).at(kResponseData).value("access_token", "");
        if (token.empty()) return TcErr(CmsApiError::kParseJsonFailed);
        return token;
    } catch (const std::exception& error) {
        LOGE("CreateGuestSession parse failed: {}", error.what());
        return TcErr(CmsApiError::kParseJsonFailed);
    }
}

px::Result<std::vector<CmsUserApplication>, CmsApiError>
CmsUserAppApi::QueryApps(const std::string& host, int port, const std::string& access_token,
                         bool guest) {
    const auto client = MakeCmsHttpClient(host, port,
        guest ? "/api/v1/public/apps" : "/api/v1/user/apps", 3000);
    client->SetHeader("Authorization", "Bearer " + access_token);
    const auto response = client->Request();
    if (response.status != 200 || response.body.empty()) {
        return HttpError<std::vector<CmsUserApplication>>("QueryApps", response.status);
    }
    try {
        const auto data = json::parse(response.body).at(kResponseData);
        std::vector<CmsUserApplication> apps;
        for (const auto& item : data) {
            CmsUserApplication app;
            app.app_id = item.value("app_id", "");
            app.name = item.value("name", "");
            app.access_mode = item.value("access_mode", "public");
            if (item.contains("running_instance") && !item["running_instance"].is_null()) {
                app.running_instance = std::make_shared<CmsUserAppInstance>(ParseInstance(item["running_instance"]));
            }
            if (!app.app_id.empty()) apps.push_back(std::move(app));
        }
        return apps;
    } catch (const std::exception& error) {
        LOGE("QueryApps parse failed: {}", error.what());
        return TcErr(CmsApiError::kParseJsonFailed);
    }
}

px::Result<CmsUserAppInstance, CmsApiError>
CmsUserAppApi::StartApp(const std::string& host, int port, const std::string& access_token,
                        const std::string& app_id, const std::string& client_nonce, bool guest) {
    const auto path = guest
        ? std::format("/api/v1/public/apps/{}/start", app_id)
        : std::format("/api/v1/user/apps/{}/start", app_id);
    const auto client = MakeCmsHttpClient(host, port, path, 30000);
    client->SetHeader("Authorization", "Bearer " + access_token);
    const auto response = client->Post({}, json{{"client_nonce", client_nonce}}.dump(), "application/json");
    // CMS returns 200 when an idempotent start reuses an instance and 202
    // while a newly scheduled instance is starting. Both are successful.
    if ((response.status != 200 && response.status != 202) || response.body.empty()) {
        return HttpError<CmsUserAppInstance>("StartApp", response.status);
    }
    try {
        return ParseInstance(json::parse(response.body).at(kResponseData));
    } catch (const std::exception& error) {
        LOGE("StartApp parse failed: {}", error.what());
        return TcErr(CmsApiError::kParseJsonFailed);
    }
}

px::Result<CmsConnectionTicket, CmsApiError>
CmsUserAppApi::IssueInstanceTicket(const std::string& host, int port,
                                   const std::string& access_token,
                                   const std::string& instance_id,
                                   const std::string& client_nonce,
                                   const std::vector<std::string>& requested_permissions,
                                   bool guest) {
    const auto path = guest
        ? std::format("/api/v1/public/instances/{}/ticket", instance_id)
        : std::format("/api/v1/user/instances/{}/ticket", instance_id);
    const auto client = MakeCmsHttpClient(host, port, path, 3000);
    client->SetHeader("Authorization", "Bearer " + access_token);
    const auto response = client->Post({}, json{{"client_nonce", client_nonce},
        {"requested_permissions", requested_permissions}}.dump(), "application/json");
    if (response.status != 200 || response.body.empty()) {
        return HttpError<CmsConnectionTicket>("IssueInstanceTicket", response.status);
    }
    try {
        const auto data = json::parse(response.body).at(kResponseData);
        CmsConnectionTicket ticket;
        ticket.ticket = data.value("ticket", "");
        ticket.launch_url = data.value("launch_url", "");
        ticket.expires_at = data.value("expires_at", 0LL);
        ticket.permissions = data.value("permissions", std::vector<std::string>{});
        if (ticket.ticket.empty() || ticket.launch_url.empty()) {
            return TcErr(CmsApiError::kParseJsonFailed);
        }
        return ticket;
    } catch (const std::exception& error) {
        LOGE("IssueInstanceTicket parse failed: {}", error.what());
        return TcErr(CmsApiError::kParseJsonFailed);
    }
}

px::Result<CmsUserAppInstance, CmsApiError>
CmsUserAppApi::StopInstance(const std::string& host, int port, const std::string& access_token,
                            const std::string& instance_id, bool guest) {
    const auto path = guest
        ? std::format("/api/v1/public/instances/{}/stop", instance_id)
        : std::format("/api/v1/user/instances/{}/stop", instance_id);
    const auto client = MakeCmsHttpClient(host, port, path, 5000);
    client->SetHeader("Authorization", "Bearer " + access_token);
    const auto response = client->Post({}, "{}", "application/json");
    if (response.status != 200 || response.body.empty()) {
        return HttpError<CmsUserAppInstance>("StopInstance", response.status);
    }
    try {
        return ParseInstance(json::parse(response.body).at(kResponseData));
    } catch (const std::exception& error) {
        LOGE("StopInstance parse failed: {}", error.what());
        return TcErr(CmsApiError::kParseJsonFailed);
    }
}

}
