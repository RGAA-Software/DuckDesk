//
// Created by RGAA on 27/03/2025.
//

#ifndef GAMMARAY_HTTP_BASE_OP_H
#define GAMMARAY_HTTP_BASE_OP_H

#include <string>
#include "px_common_new/expected.h"

namespace px
{
    class HttpBaseOp {
    public:
        static Result<std::string, bool> CanPingServer(bool ssl, const std::string& host, int port, const std::string& appkey);
    };
}

#endif //GAMMARAY_HTTP_BASE_OP_H
