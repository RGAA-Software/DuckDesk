//
// Created by RGAA on 18/08/2026.
//

#ifndef PIXELSPREMIUM_CONSOLE_HTTP_CLIENT_H
#define PIXELSPREMIUM_CONSOLE_HTTP_CLIENT_H

#include <memory>
#include <string>

namespace px {
class HttpClient;
}

namespace px_console {

// Console connections always use HTTPS. The retained setter cannot disable it.
void SetConsoleSslEnabled(bool enabled);
bool IsConsoleSslEnabled();

// Make an HTTPS client to Console without requiring certificate or hostname validation.
std::shared_ptr<px::HttpClient> MakeConsoleHttpClient(const std::string& host, int port, const std::string& path, int timeout_ms = 2000);
void SetPanelRequestHeaders(const std::shared_ptr<px::HttpClient>& client, const std::string& access_token = {},
                            const std::string& subject_kind = {});

}  // namespace px_console

#endif  // PIXELSPREMIUM_CONSOLE_HTTP_CLIENT_H
