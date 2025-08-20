//
// Created by RGAA on 20/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_CONN_MANAGER_H
#define GAMMARAYPREMIUM_GR_CONN_MANAGER_H

#include <memory>

namespace tc
{

    class GrSpvrContext;

    class GrConnManager {
    public:
        explicit GrConnManager(const std::shared_ptr<GrSpvrContext>& ctx);

    private:
        std::shared_ptr<GrSpvrContext> context_ = nullptr;

    };

}

#endif //GAMMARAYPREMIUM_GR_CONN_MANAGER_H
