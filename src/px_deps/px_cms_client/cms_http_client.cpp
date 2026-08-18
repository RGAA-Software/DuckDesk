//
// Created by RGAA on 18/08/2026.
//

#include "cms_http_client.h"
#include <atomic>
#include "px_common_new/http_client.h"

namespace px_cms
{

    static std::atomic_bool g_cms_ssl_enabled = true;

    void SetCmsSslEnabled(bool enabled) {
        g_cms_ssl_enabled = enabled;
    }

    bool IsCmsSslEnabled() {
        return g_cms_ssl_enabled;
    }

    std::shared_ptr<px::HttpClient> MakeCmsHttpClient(const std::string& host, int port, const std::string& path, int timeout_ms) {
        if (IsCmsSslEnabled()) {
            return px::HttpClient::MakeSSL(host, port, path, timeout_ms);
        }
        return px::HttpClient::Make(host, port, path, timeout_ms);
    }

}
