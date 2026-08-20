//
// Created by RGAA on 28/11/2025.
//

#ifndef GAMMARAYPREMIUM_CMS_USER_DEVICE_API_H
#define GAMMARAYPREMIUM_CMS_USER_DEVICE_API_H

#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "px_common_new/expected.h"
#include "cms_errors.h"

namespace px_cms
{

    class CmsUserDevice;

    struct CmsConnectionTicket {
        std::string ticket;
        std::string launch_url;
        int64_t expires_at = 0;
        std::vector<std::string> permissions;
    };

    class CmsUserDeviceApi {
    public:
        // query user-devices
        static
        px::Result<std::vector<std::shared_ptr<CmsUserDevice>>, CmsApiError>
        QueryUserBindDevices(const std::string& host,
                             int port,
                             const std::string& access_token);

        // Issue a short-lived, one-time device connection ticket.
        static
        px::Result<CmsConnectionTicket, CmsApiError>
        IssueDeviceTicket(const std::string& host,
                          int port,
                          const std::string& access_token,
                          const std::string& device_id,
                          const std::string& client_nonce,
                          const std::vector<std::string>& requested_permissions);
    };

}
#endif //GAMMARAYPREMIUM_CMS_USER_DEVICE_API_H
