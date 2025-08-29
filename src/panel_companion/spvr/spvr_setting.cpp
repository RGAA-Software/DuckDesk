//
// Created by RGAA on 29/08/2025.
//

#include "spvr_setting.h"

namespace tc
{

    void SpvrSettings::UpdateServerConfig(const std::string &host, int port) {
        this->host_ = host;
        this->port_ = port;
    }

}
