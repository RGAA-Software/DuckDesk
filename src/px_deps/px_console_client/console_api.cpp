#include "console_api.h"

#include <nlohmann/json.hpp>

#include "console_http_client.h"
#include "px_common/http_client.h"
#include "px_common/log.h"

namespace px_console {
namespace {

ConsoleApiError ParseConsoleHttpError(const px::HttpResponse& response) {
    SetConsoleApiLastErrorMessage("");
    if (!response.body.empty()) {
        try {
            const auto object = nlohmann::json::parse(response.body);
            SetConsoleApiLastErrorMessage(object.value("message", ""));
            const auto code = object.value("code", std::string{});
            if (code == "invalid_input") return ConsoleApiError::kInvalidParams;
            if (code == "unauthorized") return ConsoleApiError::kAuthenticationRequired;
            if (code == "rejected") return ConsoleApiError::kForbidden;
            if (code == "not_found") return ConsoleApiError::kNotFound;
            if (code == "conflict") return ConsoleApiError::kConflict;
            if (code == "rate_limited") return ConsoleApiError::kRateLimited;
            if (code == "unavailable") return ConsoleApiError::kServiceUnavailable;
        } catch (const std::exception& error) {
            LOGE("Console error response parsing failed: {}", error.what());
        }
    }

    if (response.status <= 0) {
        SetConsoleApiLastErrorMessage(response.error_message.empty() ? "The Console did not return a response." : response.error_message);
        return ConsoleApiError::kNetworkUnavailable;
    }
    switch (response.status) {
        case 400:
            return ConsoleApiError::kInvalidParams;
        case 401:
            return ConsoleApiError::kAuthenticationRequired;
        case 403:
            return ConsoleApiError::kForbidden;
        case 404:
            return ConsoleApiError::kNotFound;
        case 409:
            return ConsoleApiError::kConflict;
        case 410:
            return ConsoleApiError::kGone;
        case 429:
            return ConsoleApiError::kRateLimited;
        case 503:
            return ConsoleApiError::kServiceUnavailable;
        default:
            if (ConsoleApiLastErrorMessage().empty()) SetConsoleApiLastErrorMessage("HTTP " + std::to_string(response.status));
            return ConsoleApiError::kInternalError;
    }
}

}  // namespace

ConsoleApiError ToConsoleUserApiError(const px::HttpResponse& response) { return ParseConsoleHttpError(response); }

px::Result<bool, ConsoleApiError> QueryConsoleReady(const std::string& host, const int port, const std::shared_ptr<std::atomic_bool>& cancellation) {
    const auto client = MakeConsoleHttpClient(host, port, "/health/ready", 3'000);
    client->SetCancellationSignal(cancellation);
    const auto response = client->Request();
    if (response.status == 204) return true;
    return TcErr(ParseConsoleHttpError(response));
}

}  // namespace px_console
