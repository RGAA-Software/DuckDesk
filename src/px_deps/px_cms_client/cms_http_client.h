//
// Created by RGAA on 18/08/2026.
//

#ifndef GAMMARAYPREMIUM_CMS_HTTP_CLIENT_H
#define GAMMARAYPREMIUM_CMS_HTTP_CLIENT_H

#include <memory>
#include <string>

namespace px
{
    class HttpClient;
}

namespace px_cms
{

    // whether the CMS server requires ssl(https), default true for old deployments.
    // the panel process syncs this switch from PxSettings(cms_ssl_enable).
    void SetCmsSslEnabled(bool enabled);
    bool IsCmsSslEnabled();

    // make a http(s) client to CMS, scheme selected by IsCmsSslEnabled()
    std::shared_ptr<px::HttpClient> MakeCmsHttpClient(const std::string& host, int port, const std::string& path, int timeout_ms = 2000);

}

#endif //GAMMARAYPREMIUM_CMS_HTTP_CLIENT_H
