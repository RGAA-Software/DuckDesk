//
// Created by RGAA on 18/08/2026.
//

#include "console_http_client.h"

#include <atomic>
#include "px_common/http_client.h"

namespace px_console {

static std::atomic_bool g_console_ssl_enabled = true;

void SetConsoleSslEnabled(bool /*enabled*/) {
    // Console is HTTPS/WSS-only. Keep the setter for source compatibility
    // with access-info parsers from older deployments, but never downgrade.
    g_console_ssl_enabled = true;
}

bool IsConsoleSslEnabled() { return g_console_ssl_enabled; }

std::shared_ptr<px::HttpClient> MakeConsoleHttpClient(const std::string& host, int port, const std::string& path, int timeout_ms) {
    if (IsConsoleSslEnabled()) {
        auto client = px::HttpClient::MakeSSL(host, port, path, timeout_ms);
        // Console still uses HTTPS, but clients accept private self-signed certificates.
        client->SetVerifySsl(false);
        return client;
    }
    return px::HttpClient::Make(host, port, path, timeout_ms);
}

void SetPanelRequestHeaders(const std::shared_ptr<px::HttpClient>& client, const std::string& access_token, const std::string& subject_kind) {
    if (!client) {
        return;
    }
    client->SetHeader("X-Pixels-Client-Type", "panel");
    if (!access_token.empty()) {
        client->SetHeader("Authorization", "Bearer " + access_token);
    }
    if (!subject_kind.empty()) {
        client->SetHeader("X-Pixels-Subject-Kind", subject_kind);
    }
}

}  // namespace px_console
