//
// Created by RGAA on 2024/3/15.
//

#include "server_cast.h"
#include "px_common/log.h"
#include "px_context.h"

namespace px
{

    std::shared_ptr<ServerCast> ServerCast::Make(std::shared_ptr<PxContext> &ctx) {
        return std::make_shared<ServerCast>(ctx);
    }

    ServerCast::ServerCast(const std::shared_ptr<PxContext> &ctx) : context_(ctx) {

    }

    void ServerCast::Start() {

    }

    void ServerCast::Stop() {

    }

}
