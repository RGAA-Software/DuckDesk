//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_PROFILE_SETTINGS_H
#define GAMMARAYPREMIUM_GR_PROFILE_SETTINGS_H

#include <string>

namespace tc
{

    class GrProfileSettings {
    public:
        static GrProfileSettings* Instance() {
            static GrProfileSettings instance;
            return &instance;
        }

        void LoadSettings();

    public:
        std::string mongo_url_;
        std::string mongo_db_name_;

    };

}

#endif //GAMMARAYPREMIUM_GR_PROFILE_SETTINGS_H
