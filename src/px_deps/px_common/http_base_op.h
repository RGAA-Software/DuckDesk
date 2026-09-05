//
// Created by RGAA on 27/03/2025.
//

#ifndef PX_HTTP_BASE_OP_H
#define PX_HTTP_BASE_OP_H

#include <string>
#include "px_common/expected.h"

namespace px
{
    class HttpBaseOp {
    public:
        static Result<std::string, bool> CanPingServer(bool ssl, const std::string& host, int port, const std::string& appkey);
    };
}

#endif //PX_HTTP_BASE_OP_H
