//
// Created by RGAA on 23/01/2026.
//

#ifndef GAMMARAYPREMIUM_CMS_EVENT_API_H
#define GAMMARAYPREMIUM_CMS_EVENT_API_H

#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "cms_server_info.h"
#include "px_common_new/expected.h"
#include "cms_errors.h"

using namespace px_cms;

namespace px
{

    class CmsEvent;
    using CmsEventPtr = std::shared_ptr<CmsEvent>;

    class CmsEventApi {
    public:
        // Cpu Event
        static Result<CmsEventPtr, CmsApiError>
        AddEvent(const std::string& host,
                 int port,
                 const std::string& appkey,
                 const CmsEventPtr& event);
    };

}

#endif //GAMMARAYPREMIUM_CMS_EVENT_API_H