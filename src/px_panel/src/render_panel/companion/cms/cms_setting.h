//
// Created by RGAA on 29/08/2025.
//

#ifndef GAMMARAYPREMIUM_CMS_SETTING_H
#define GAMMARAYPREMIUM_CMS_SETTING_H

#include <string>

namespace px
{

    class CmsSettings {
    public:
        static CmsSettings* Instance() {
            static CmsSettings instance;
            return &instance;
        }

        void UpdateServerConfig(const std::string &host, int port, bool ssl_enable);

    public:
        std::string host_;
        int port_ = 0;
        // whether the cms server requires ssl(https/wss), default true for old deployments
        bool ssl_enable_ = true;
    };

}

#endif //GAMMARAYPREMIUM_CMS_SETTING_H
