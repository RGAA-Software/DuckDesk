//
// Created by RGAA on 23/01/2026.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_EVENT_API_H
#define GAMMARAYPREMIUM_CONSOLE_EVENT_API_H

#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "console_server_info.h"
#include "px_common_new/expected.h"
#include "console_errors.h"

using namespace px_console;

namespace px
{

    class ConsoleEvent;
    using ConsoleEventPtr = std::shared_ptr<ConsoleEvent>;

    class ConsoleEventApi {
    public:
        // Cpu Event
        static Result<ConsoleEventPtr, ConsoleApiError>
        AddEvent(const std::string& host,
                 int port,
                 const std::string& appkey,
                 const ConsoleEventPtr& event);
    };

}

#endif //GAMMARAYPREMIUM_CONSOLE_EVENT_API_H