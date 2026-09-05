//
// Created by RGAA on 12/12/2025.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_API_H
#define GAMMARAYPREMIUM_CONSOLE_API_H

#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "px_common/expected.h"
#include "console_errors.h"

namespace px
{
    class HttpResponse;
}

namespace px_console
{

    // Console error responses carry a business code in their JSON body.  The
    // HTTP status alone is not sufficient (for example DeviceNotFound is sent
    // as HTTP 400 with business code 602).
    ConsoleApiError ToConsoleApiError(const px::HttpResponse& response);

    // User and guest endpoints use normal HTTP authentication semantics, while
    // the Console control endpoints use 401/403 for app-key and quota errors.
    ConsoleApiError ToConsoleUserApiError(const px::HttpResponse& response);

    // Alive Connections
    class AliveConnections {
    public:
        int total_ = 0;
        int relay_ = 0;
    };

    // Available New Connection
    class AvailableNewConnection {
    public:
        bool available_ = false;
    };

    // Api
    class ConsoleApi {
    public:
        // query alive connections
        static px::Result<AliveConnections, ConsoleApiError>
        QueryAliveConnections(const std::string& host, int port, const std::string& appkey);

        // query available new connection
        static px::Result<AvailableNewConnection, ConsoleApiError>
        QueryAvailableNewConnection(const std::string& host, int port, const std::string& appkey);

    };

}

#endif //GAMMARAYPREMIUM_CONSOLE_API_H
