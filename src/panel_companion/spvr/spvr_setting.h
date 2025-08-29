//
// Created by RGAA on 29/08/2025.
//

#ifndef GAMMARAYPREMIUM_SPVR_SETTING_H
#define GAMMARAYPREMIUM_SPVR_SETTING_H

#include <string>

namespace tc
{

    class SpvrSettings {
    public:
        static SpvrSettings* Instance() {
            static SpvrSettings instance;
            return &instance;
        }

        void UpdateServerConfig(const std::string &host, int port);

    public:
        std::string host_;
        int port_ = 0;
    };

}

#endif //GAMMARAYPREMIUM_SPVR_SETTING_H
