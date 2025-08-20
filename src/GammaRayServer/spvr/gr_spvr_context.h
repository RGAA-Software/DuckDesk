//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_SPVR_CONTEXT_H
#define GAMMARAYPREMIUM_GR_SPVR_CONTEXT_H

#include <memory>

namespace tc
{

    class GrSpvrDatabase;
    class GrConnManager;
    class GrSpvrAuthManager;

    class GrSpvrContext : public std::enable_shared_from_this<GrSpvrContext> {
    public:
        GrSpvrContext();
        bool Init();

        std::shared_ptr<GrSpvrDatabase> GetDatabase();
        std::shared_ptr<GrConnManager> GetConnManager();
        std::shared_ptr<GrSpvrAuthManager> GetAuthManager();

    private:
        std::shared_ptr<GrSpvrDatabase> db_ = nullptr;
        std::shared_ptr<GrConnManager> conn_mgr_ = nullptr;
        std::shared_ptr<GrSpvrAuthManager> auth_mgr_ = nullptr;

    };

    extern std::shared_ptr<GrSpvrContext> grSpvrContext;

}

#endif //GAMMARAYPREMIUM_GR_PROFILE_CONTEXT_H
