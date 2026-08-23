//
// Created by RGAA on 29/08/2025.
//

#include "console_setting.h"

namespace px
{

    void ConsoleSettings::UpdateServerConfig(const std::string &host, int port, bool ssl_enable) {
        this->host_ = host;
        this->port_ = port;
        this->ssl_enable_ = ssl_enable;
    }

}
