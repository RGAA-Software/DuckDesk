//
// Created by RGAA on 20/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_SPVR_AUTH_MANAGER_H
#define GAMMARAYPREMIUM_GR_SPVR_AUTH_MANAGER_H

#include <memory>
#include <string>

namespace tc
{

    class GrSpvrAuth;
    class GrSpvrContext;

    class GrSpvrAuthManager {
    public:
        explicit GrSpvrAuthManager(const std::shared_ptr<GrSpvrContext>& ctx);
        bool Init();
        bool UpdateAuth(const std::string& auth);
        std::string GetAppkey();

    private:
        void LoadDefaultAuth();

    private:
        std::shared_ptr<GrSpvrContext> context_ = nullptr;
        std::shared_ptr<GrSpvrAuth> spvr_auth_ = nullptr;
    };

}


#endif //GAMMARAYPREMIUM_GR_SPVR_AUTH_MANAGER_H
