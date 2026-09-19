#include "console_resource_api.h"

#include <algorithm>
#include <format>
#include <nlohmann/json.hpp>
#include <string_view>

#include "console_api.h"
#include "console_http_client.h"
#include "px_common/http_client.h"
#include "px_common/log.h"

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

json TargetPayload(const ConsoleResourceTarget& target) {
    if (target.kind == ConsoleResourceTargetKind::Desktop) {
        return {{"kind", "desktop"}, {"device_id", target.device_id}};
    }
    return {{"kind", "cloud_application"}, {"application_id", target.application_id}, {"instance_id", target.instance_id}};
}

bool SameTarget(const json& actual, const ConsoleResourceTarget& expected) {
    if (!actual.is_object()) {
        return false;
    }
    if (expected.kind == ConsoleResourceTargetKind::Desktop) {
        return actual.value("kind", "") == "desktop" && actual.value("device_id", "") == expected.device_id;
    }
    return actual.value("kind", "") == "cloud_application" && actual.value("application_id", "") == expected.application_id &&
           actual.value("instance_id", "") == expected.instance_id;
}

}  // namespace

px::Result<ConsoleResourceConnection, ConsoleApiError> OpenPanelResourceConnection(const std::string& host, const int port,
                                                                                   const std::string& access_token, const bool guest,
                                                                                   const ConsoleResourceTarget& target, const bool view_only,
                                                                                   const std::string& request_id) {
    const auto subject = guest ? "guest" : "user";
    const auto access = view_only ? "observer" : "controller";
    const auto target_payload = TargetPayload(target);
    const auto open_client = MakeConsoleHttpClient(host, port, "/api/console/resource-sessions", 5'000);
    SetPanelRequestHeaders(open_client, access_token, subject);
    const auto open_response =
        open_client->Post({}, json{{"request_id", request_id}, {"target", target_payload}, {"access", access}}.dump(), "application/json");
    if (open_response.status != 201 || open_response.body.empty()) {
        return HttpError<ConsoleResourceConnection>("OpenResourceSession", open_response);
    }

    try {
        const auto opened = json::parse(open_response.body);
        const auto session_id = opened.value("id", "");
        const auto opened_revision = opened.value("revision", 0LL);
        if (session_id.empty() || opened_revision <= 0 || opened.value("client_type", "") != "panel" || opened.value("access_role", "") != access ||
            !SameTarget(opened.at("target"), target)) {
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }

        const auto descriptor_client =
            MakeConsoleHttpClient(host, port, std::format("/api/console/resource-sessions/{}/descriptor", session_id), 5'000);
        SetPanelRequestHeaders(descriptor_client, access_token, subject);
        auto descriptor_response = descriptor_client->Post({}, json{{"revision", opened_revision}}.dump(), "application/json");
        if (descriptor_response.status != 200 || descriptor_response.body.empty()) {
            return HttpError<ConsoleResourceConnection>("IssueResourceDescriptor", descriptor_response);
        }

        auto payload = json::parse(descriptor_response.body);
        std::fill(descriptor_response.body.begin(), descriptor_response.body.end(), '\0');
        descriptor_response.body.clear();
        const auto& descriptor = payload.at("descriptor");
        const auto& session = descriptor.at("session");
        auto frontend_token = payload.at("token").get<std::string>();
        payload["token"] = nullptr;
        const auto descriptor_revision = session.value("revision", 0LL);
        ConsoleResourceConnection result{
            .host = descriptor.value("host", ""),
            .port = descriptor.value("port", 0),
            .remote_resource_id = target.kind == ConsoleResourceTargetKind::Desktop ? target.device_id : target.instance_id,
            .session_id = session.value("id", ""),
            .session_revision = descriptor_revision,
            .frontend_token = px::SecretBuffer::Take(std::move(frontend_token)),
            .transport = descriptor.value("transport", "")};
        if (const auto relay = payload.find("relay"); relay != payload.end() && !relay->is_null()) {
            result.relay_host = relay->value("host", "");
            result.relay_port = relay->value("port", 0);
            result.relay_admission_ticket = relay->value("admission_ticket", "");
            if (result.relay_host.empty() || result.relay_port <= 0 || result.relay_port > 65'535 || result.relay_admission_ticket.size() < 64 ||
                result.relay_admission_ticket.size() > 256) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
        }
        const auto expected_owner = guest ? "guest" : "user";
        const auto& owner = session.at("owner");
        if (result.host.empty() || result.port <= 0 || result.port > 65'535 || result.remote_resource_id.empty() || result.session_id != session_id ||
            result.session_revision < opened_revision || !result.frontend_token || result.frontend_token->Bytes().empty() ||
            result.transport != "native" || session.value("client_type", "") != "panel" || session.value("access_role", "") != access ||
            (session.value("state", "") != "pending" && session.value("state", "") != "connected") || owner.value("kind", "") != expected_owner ||
            !SameTarget(session.at("target"), target)) {
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
        return result;
    } catch (const std::exception& error) {
        LOGE("Resource descriptor response parsing failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<bool, ConsoleApiError> ClosePanelResourceConnection(const std::string& host, const int port, const std::string& access_token,
                                                               const bool guest, const std::string& session_id, const std::int64_t session_revision) {
    if (session_id.empty() || session_revision <= 0) {
        return TcErr(ConsoleApiError::kInvalidParams);
    }
    const auto client = MakeConsoleHttpClient(host, port, std::format("/api/console/resource-sessions/{}/close", session_id), 5'000);
    SetPanelRequestHeaders(client, access_token, guest ? "guest" : "user");
    const auto response = client->Post({}, json{{"revision", session_revision}}.dump(), "application/json");
    if (response.status != 200 || response.body.empty()) {
        return HttpError<bool>("CloseResourceSession", response);
    }
    try {
        const auto session = json::parse(response.body);
        const auto state = session.value("state", "");
        return session.value("id", "") == session_id && (state == "closing" || state == "closed")
                   ? px::Result<bool, ConsoleApiError>{true}
                   : px::Result<bool, ConsoleApiError>{TcErr(ConsoleApiError::kParseJsonFailed)};
    } catch (const std::exception& error) {
        LOGE("Close resource session response parsing failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

}  // namespace px_console
