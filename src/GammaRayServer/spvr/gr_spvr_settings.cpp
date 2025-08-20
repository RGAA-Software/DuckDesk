//
// Created by RGAA on 19/08/2025.
//

#include "gr_spvr_settings.h"

namespace tc
{

    void GrSpvrSettings::LoadSettings() {
        mongo_url_ = "mongodb://localhost:27017/";
        mongo_db_name_ = "gr_profile_db";
    }

}