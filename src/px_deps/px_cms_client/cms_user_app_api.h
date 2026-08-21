#ifndef GAMMARAYPREMIUM_CMS_USER_APP_API_H
#define GAMMARAYPREMIUM_CMS_USER_APP_API_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cms_errors.h"
#include "cms_user_device_api.h"
#include "px_common_new/expected.h"

namespace px_cms {

struct CmsUserAppInstance {
    std::string instance_id;
    std::string state;
    std::string error_code;
    bool reconnectable = false;
};

struct CmsUserApplication {
    std::string app_id;
    std::string name;
    std::string access_mode;
    std::string cover_url;
    int64_t version = 0;
    std::shared_ptr<CmsUserAppInstance> running_instance;
};

class CmsUserAppApi {
public:
    static px::Result<std::string, CmsApiError>
    CreateGuestSession(const std::string& host, int port, const std::string& client_nonce);

    static px::Result<std::vector<CmsUserApplication>, CmsApiError>
    QueryApps(const std::string& host, int port, const std::string& access_token, bool guest = false);

    static px::Result<CmsUserAppInstance, CmsApiError>
    StartApp(const std::string& host, int port, const std::string& access_token,
             const std::string& app_id, const std::string& client_nonce, bool guest = false);

    static px::Result<CmsConnectionTicket, CmsApiError>
    IssueInstanceTicket(const std::string& host, int port, const std::string& access_token,
                        const std::string& instance_id, const std::string& client_nonce,
                        const std::vector<std::string>& requested_permissions, bool guest = false);

    static px::Result<CmsUserAppInstance, CmsApiError>
    StopInstance(const std::string& host, int port, const std::string& access_token,
                 const std::string& instance_id, bool guest = false);
};

}
#endif
