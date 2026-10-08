#include "console_resource_api.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <nlohmann/json.hpp>
#include <string_view>

#include "console_api.h"
#include "console_http_client.h"
#include "px_common/http_client.h"
#include "px_common/log.h"
#include "px_common/scope_exit.h"
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

bool IsRdpAccountName(const std::string_view value) {
    constexpr std::string_view prefix{"pxrdp_"};
    return value.size() == 20U && value.starts_with(prefix) &&
           std::all_of(value.begin() + prefix.size(), value.end(),
                       [](const unsigned char character) { return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f'); });
}

bool IsWindowsDomain(const std::string_view value) {
    return !value.empty() && value.size() <= 15U &&
           std::all_of(value.begin(), value.end(), [](const unsigned char character) { return std::isalnum(character) != 0 || character == '-'; });
}

bool IsSha256(const std::string_view value) {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
               return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f');
           });
}

}  // namespace

px::Result<ConsoleResourceConnection, ConsoleApiError> OpenPanelResourceConnection(const std::string& host, const int port,
                                                                                   const std::string& access_token, const bool guest,
                                                                                   const ConsoleResourceTarget& target, const bool view_only,
                                                                                   const std::string& request_id) {
    if (!px::IsCanonicalUUID(request_id)) {
        LOGE("OpenResourceSession rejected locally: field=request_id code=invalid_uuid_format");
        return TcErr(ConsoleApiError::kInvalidParams);
    }
    const std::string subject{guest ? "guest" : "user"};
    const std::string access{view_only ? "observer" : "controller"};
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
        if (!px::IsCanonicalUUID(session_id) || opened_revision <= 0) return TcErr(ConsoleApiError::kParseJsonFailed);
        LOGI("Resource connection reserved: request={} session={} revision={}", request_id, session_id, opened_revision);
        // Until a complete descriptor reaches the caller, this scope owns the reservation.
        auto release_reservation = px::PxScopeExit{[host, port, access_token, guest, session_id, opened_revision] {
            const auto original_error = ConsoleApiLastErrorMessage();
            try {
                const auto closed = ClosePanelResourceConnection(host, port, access_token, guest, session_id, opened_revision);
                if (!closed || !*closed) LOGW("Unlaunched resource reservation could not be closed: {}", session_id);
            } catch (...) {
                LOGW("Unlaunched resource reservation cleanup failed: {}", session_id);
            }
            SetConsoleApiLastErrorMessage(original_error);
        }};
        if (opened.value("client_type", "") != "panel" || opened.value("access_role", "") != access || !SameTarget(opened.at("target"), target)) {
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
        if (result.transport == "rdp") {
            const auto rdp = payload.find("rdp");
            if (rdp == payload.end() || !rdp->is_object() || rdp->value("schema", 0) != 1) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            const auto passwordEntry = rdp->find("password");
            if (passwordEntry == rdp->end() || !passwordEntry->is_string()) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            auto& sourcePassword = passwordEntry->get_ref<std::string&>();
            auto password = px::SecretBuffer::Take(sourcePassword);
            std::fill(sourcePassword.begin(), sourcePassword.end(), '\0');
            const auto account = rdp->value("account_name", "");
            const auto domain = rdp->value("domain", "");
            const auto certificate = rdp->value("proxy_certificate_sha256", "");
            payload["rdp"] = nullptr;
            if (!password || password->Bytes().empty() || !IsRdpAccountName(account) || !IsWindowsDomain(domain) || !IsSha256(certificate)) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
            auto configuration_payload = json{{"schema", 1},
                                              {"account_name", account},
                                              {"domain", domain},
                                              {"proxy_certificate_sha256", certificate},
                                              {"password", std::string{password->View()}}};
            auto configuration = configuration_payload.dump();
            auto& configuration_password = configuration_payload["password"].get_ref<std::string&>();
            std::fill(configuration_password.begin(), configuration_password.end(), '\0');
            result.rdp_configuration = px::SecretBuffer::Take(std::move(configuration));
        } else if (result.transport != "native" || (payload.contains("rdp") && !payload["rdp"].is_null())) {
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
        if (const auto relay = payload.find("relay"); relay != payload.end() && !relay->is_null()) {
            result.relay_host = relay->value("host", "");
            result.relay_port = relay->value("port", 0);
            result.relay_admission_ticket = relay->value("admission_ticket", "");
            if (result.relay_host.empty() || result.relay_port <= 0 || result.relay_port > 65'535 || result.relay_admission_ticket.size() < 64 ||
                result.relay_admission_ticket.size() > 256) {
                return TcErr(ConsoleApiError::kParseJsonFailed);
            }
        }
        const std::string expected_owner{guest ? "guest" : "user"};
        const auto& owner = session.at("owner");
        if (result.host.empty() || result.port <= 0 || result.port > 65'535 || result.remote_resource_id.empty() || result.session_id != session_id ||
            result.session_revision < opened_revision || !result.frontend_token || result.frontend_token->Bytes().empty() ||
            (result.transport == "rdp") != static_cast<bool>(result.rdp_configuration) || session.value("client_type", "") != "panel" ||
            session.value("access_role", "") != access || (session.value("state", "") != "pending" && session.value("state", "") != "connected") ||
            owner.value("kind", "") != expected_owner || !SameTarget(session.at("target"), target)) {
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
        LOGI("Resource descriptor ready: request={} session={} revision={} transport={}", request_id, session_id,
             result.session_revision, result.transport);
        release_reservation.Release();
        return result;
    } catch (const std::exception& error) {
        LOGE("Resource descriptor response parsing failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<bool, ConsoleApiError> ClosePanelResourceConnection(const std::string& host, const int port, const std::string& access_token,
                                                               const bool guest, const std::string& session_id, const std::int64_t session_revision) {
    if (!px::IsCanonicalUUID(session_id) || session_revision <= 0) {
        return TcErr(ConsoleApiError::kInvalidParams);
    }
    try {
        const auto session_path = std::format("/api/console/resource-sessions/{}", session_id);
        const auto query_client = MakeConsoleHttpClient(host, port, session_path, 5'000);
        const auto close_client = MakeConsoleHttpClient(host, port, session_path + "/close", 5'000);
        SetPanelRequestHeaders(query_client, access_token, guest ? "guest" : "user");
        SetPanelRequestHeaders(close_client, access_token, guest ? "guest" : "user");
        std::int64_t rejected_revision{};
        // Descriptor issuance and frontend confirmation both advance the revision.
        // Retry a concurrent transition only when a read proves the revision changed.
        for (int close_attempt{}; close_attempt < 3; ++close_attempt) {
            const auto current_response = query_client->Request();
            if (current_response.status != 200 || current_response.body.empty())
                return HttpError<bool>("ReadResourceSessionForClose", current_response);
            const auto current = json::parse(current_response.body);
            const auto current_revision = current.value("revision", 0LL);
            const auto current_state = current.value("state", "");
            if (current.value("id", "") != session_id || current_revision <= 0) return TcErr(ConsoleApiError::kParseJsonFailed);
            LOGI("Resource connection cleanup: session={} supplied_revision={} current_revision={} state={} attempt={}", session_id,
                 session_revision, current_revision, current_state, close_attempt + 1);
            if (current_state == "closing" || current_state == "closed") return true;
            if (current_revision == rejected_revision) return TcErr(ConsoleApiError::kForbidden);
            const auto response = close_client->Post({}, json{{"revision", current_revision}}.dump(), "application/json");
            if (response.status == 403) {
                static_cast<void>(ToConsoleUserApiError(response));
                rejected_revision = current_revision;
                continue;
            }
            if (response.status != 200 || response.body.empty()) return HttpError<bool>("CloseResourceSession", response);
            const auto session = json::parse(response.body);
            const auto state = session.value("state", "");
            LOGI("Resource connection close acknowledged: session={} state={} capacity_released={}", session_id, state, state == "closed");
            return session.value("id", "") == session_id && (state == "closing" || state == "closed")
                       ? px::Result<bool, ConsoleApiError>{true}
                       : px::Result<bool, ConsoleApiError>{TcErr(ConsoleApiError::kParseJsonFailed)};
        }
        return TcErr(ConsoleApiError::kConflict);
    } catch (const std::exception& error) {
        LOGE("Close resource session response parsing failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

}  // namespace px_console
