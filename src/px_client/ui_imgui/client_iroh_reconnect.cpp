#include "client_iroh_reconnect.h"

#include <cpr/cpr.h>

#include <format>
#include <nlohmann/json.hpp>

#include "px_common/log.h"
#include "px_common/uuid.h"

namespace px::client::imgui {
void BindClientIrohRefresh(std::optional<px::IrohDialParameters>& parameters, const ClientLaunchConfig& config) {
    if (!parameters || !config.frontendToken || !config.consoleOrigin.starts_with("https://") || !px::IsCanonicalUUID(config.frontendSessionId) ||
        config.frontendSessionRevision <= 0)
        return;
    auto console_origin = config.consoleOrigin;
    while (console_origin.ends_with('/')) console_origin.pop_back();
    const auto endpoint_identity = nlohmann::json::parse(parameters->endpoint_address).at("id").get<std::string>();
    const auto address_url = std::format("{}/api/console/resource-sessions/{}/iroh-endpoint", console_origin, config.frontendSessionId);
    parameters->refresh_endpoint = [address_url, endpoint_identity, credential = config.frontendToken,
                                    revision = config.frontendSessionRevision]() -> IrohEndpointRefreshResult {
        cpr::Session request{};
        request.SetUrl(cpr::Url{address_url});
        request.SetTimeout(cpr::Timeout{1500});
        request.SetRedirect(cpr::Redirect{false});
        // Match the existing Console HTTPS adapter's private-deployment trust policy.
        request.SetVerifySsl(cpr::VerifySsl{false});
        request.SetHeader(cpr::Header{{"Authorization", "Bearer " + std::string{credential->View()}}, {"Content-Type", "application/json"}});
        request.SetBody(cpr::Body{nlohmann::json{{"revision", revision}}.dump()});
        const auto response = request.Post();
        if (response.status_code != 200) {
            LOGW("event=iroh.endpoint_refresh outcome=unavailable http={}", response.status_code);
            if (response.status_code == 401 || response.status_code == 403 || response.status_code == 404 || response.status_code == 410)
                return {.terminal_error = "IROH_SESSION_ENDED"};
            if (response.status_code == 400 || response.status_code == 409) return {.terminal_error = "IROH_ENDPOINT_INVALID"};
            return {};
        }
        const auto description = nlohmann::json::parse(response.text, nullptr, false);
        const auto refreshed = px::ParseIrohConnectionDescription(description);
        if (!refreshed || description["endpoint_address"].value("id", std::string{}) != endpoint_identity) {
            LOGW("event=iroh.endpoint_refresh outcome=rejected reason=endpoint_identity_changed_or_invalid");
            return {.terminal_error = "IROH_ENDPOINT_INVALID"};
        }
        return {.description = refreshed};
    };
}
}  // namespace px::client::imgui
