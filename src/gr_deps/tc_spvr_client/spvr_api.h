//
// Created by RGAA on 12/12/2025.
//

#ifndef GAMMARAYPREMIUM_SPVR_API_H
#define GAMMARAYPREMIUM_SPVR_API_H

#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "tc_common_new/expected.h"
#include "spvr_errors.h"

namespace spvr
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
    class SpvrApi {
    public:
        // query alive connections
        static tc::Result<AliveConnections, SpvrApiError>
        QueryAliveConnections(const std::string& host, int port, const std::string& appkey);

        // query available new connection
        static tc::Result<AvailableNewConnection, SpvrApiError>
        QueryAvailableNewConnection(const std::string& host, int port, const std::string& appkey);

    };

}

#endif //GAMMARAYPREMIUM_SPVR_API_H