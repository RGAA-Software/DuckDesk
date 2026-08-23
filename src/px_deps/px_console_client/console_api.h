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

#include "px_common_new/expected.h"
#include "console_errors.h"

namespace px_console
{

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