//
// Created by RGAA on 29/08/2025.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_SETTING_H
#define GAMMARAYPREMIUM_CONSOLE_SETTING_H

#include <string>

namespace px
{

    class ConsoleSettings {
    public:
        static ConsoleSettings* Instance() {
            static ConsoleSettings instance;
            return &instance;
        }

        void UpdateServerConfig(const std::string &host, int port, bool ssl_enable);

    public:
        std::string host_;
        int port_ = 0;
        // Console is HTTPS/WSS-only. Retained as a field for existing callers.
        bool ssl_enable_ = true;
    };

}

#endif //GAMMARAYPREMIUM_CONSOLE_SETTING_H
