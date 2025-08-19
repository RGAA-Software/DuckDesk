//
// Created by RGAA on 19/08/2025.
//

#include "gr_profile_context.h"
#include "gr_profile_database.h"

namespace tc
{

    std::shared_ptr<GrProfileContext> grProfileContext = nullptr;

    GrProfileContext::GrProfileContext() {

    }

    bool GrProfileContext::Init() {
        grProfileContext = shared_from_this();

        // database
        db_ = std::make_shared<GrProfileDatabase>(shared_from_this());
        if (!db_->Init()) {
            return false;
        }


        return true;
    }

    std::shared_ptr<GrProfileDatabase> GrProfileContext::GetDatabase() {
        return db_;
    }

}