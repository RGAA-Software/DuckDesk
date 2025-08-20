//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_SPVR_SETTINGS_H
#define GAMMARAYPREMIUM_GR_SPVR_SETTINGS_H

#include <string>

namespace tc
{

    class GrSpvrSettings {
    public:
        static GrSpvrSettings* Instance() {
            static GrSpvrSettings instance;
            return &instance;
        }

        void LoadSettings();

    public:
        std::string mongo_url_;
        std::string mongo_db_name_;

    };

}

#endif //GAMMARAYPREMIUM_GR_SPVR_SETTINGS_H
