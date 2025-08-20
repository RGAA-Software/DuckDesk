//
// Created by RGAA on 20/08/2025.
//

#include "gr_spvr_auth_manager.h"
#include "gr_spvr_auth.h"

namespace tc
{

    GrSpvrAuthManager::GrSpvrAuthManager(const std::shared_ptr<GrSpvrContext>& ctx) {
        context_ = ctx;
    }

    bool GrSpvrAuthManager::Init() {
        // load configured auth

        // don't have an auth, load default
        LoadDefaultAuth();
        return true;
    }

    bool GrSpvrAuthManager::UpdateAuth(const std::string& auth) {
        // update to db

        // update the [spvr_auth_]

        return true;
    }

    std::string GrSpvrAuthManager::GetAppkey() {
        return spvr_auth_ ? spvr_auth_->appkey_ : "";
    }

    void GrSpvrAuthManager::LoadDefaultAuth() {
        spvr_auth_ = std::make_shared<GrSpvrAuth>();
        spvr_auth_->max_online_clients_ = 2;
        spvr_auth_->appkey_ = "c6b8a1d2f94e3a7b05c2d1e8f3b9a4d0";
    }

}