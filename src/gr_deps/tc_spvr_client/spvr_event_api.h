//
// Created by RGAA on 23/01/2026.
//

#ifndef GAMMARAYPREMIUM_SPVR_EVENT_API_H
#define GAMMARAYPREMIUM_SPVR_EVENT_API_H

#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "spvr_server_info.h"
#include "tc_common_new/expected.h"
#include "spvr_errors.h"

using namespace spvr;

namespace tc
{

    class SpvrEvent;
    using SpvrEventPtr = std::shared_ptr<SpvrEvent>;

    class SpvrEventApi {
    public:
        // Cpu Event
        static Result<SpvrEventPtr, SpvrApiError>
        AddEvent(const std::string& host,
                 int port,
                 const std::string& appkey,
                 const SpvrEventPtr& event);
    };

}

#endif //GAMMARAYPREMIUM_SPVR_EVENT_API_H