//
// Created by RGAA on 19/08/2025.
//

#include "gr_spvr_context.h"
#include "spvr/db/gr_spvr_database.h"
#include "spvr/ws/gr_conn_manager.h"
#include "spvr/auth/gr_spvr_auth_manager.h"
#include "tc_common_new/log.h"

namespace tc
{

    std::shared_ptr<GrSpvrContext> grSpvrContext = nullptr;

    GrSpvrContext::GrSpvrContext() {

    }

    bool GrSpvrContext::Init() {
        grSpvrContext = shared_from_this();

        // database
        db_ = std::make_shared<GrSpvrDatabase>(shared_from_this());
        if (!db_->Init()) {
            LOGE("GrSpvrDatabase init failed.");
            return false;
        }

        // conn manager
        conn_mgr_ = std::make_shared<GrConnManager>(shared_from_this());

        // auth manager
        auth_mgr_ = std::make_shared<GrSpvrAuthManager>(shared_from_this());
        if (!auth_mgr_->Init()) {
            LOGE("GrSpvrAuthManager init failed.");
            return false;
        }

        return true;
    }

    std::shared_ptr<GrSpvrDatabase> GrSpvrContext::GetDatabase() {
        return db_;
    }

    std::shared_ptr<GrConnManager> GrSpvrContext::GetConnManager() {
        return conn_mgr_;
    }

    std::shared_ptr<GrSpvrAuthManager> GrSpvrContext::GetAuthManager() {
        return auth_mgr_;
    }

}