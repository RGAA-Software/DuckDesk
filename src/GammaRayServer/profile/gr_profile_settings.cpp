//
// Created by RGAA on 19/08/2025.
//

#include "gr_profile_settings.h"

namespace tc
{

    void GrProfileSettings::LoadSettings() {
        mongo_url_ = "mongodb://localhost:27017/";
        mongo_db_name_ = "gr_profile_db";
    }

}