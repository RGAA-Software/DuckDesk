//
// Created by RGAA on 18/08/2026.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_HTTP_CLIENT_H
#define GAMMARAYPREMIUM_CONSOLE_HTTP_CLIENT_H

#include <memory>
#include <string>

namespace px
{
    class HttpClient;
}

namespace px_console
{

    // whether the Console server requires ssl(https), default true for old deployments.
    // the panel process syncs this switch from PxSettings(console_ssl_enable).
    void SetConsoleSslEnabled(bool enabled);
    bool IsConsoleSslEnabled();

    // make a http(s) client to Console, scheme selected by IsConsoleSslEnabled()
    std::shared_ptr<px::HttpClient> MakeConsoleHttpClient(const std::string& host, int port, const std::string& path, int timeout_ms = 2000);

}

#endif //GAMMARAYPREMIUM_CONSOLE_HTTP_CLIENT_H
