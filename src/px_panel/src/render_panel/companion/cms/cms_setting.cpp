//
// Created by RGAA on 29/08/2025.
//

#include "cms_setting.h"

namespace px
{

    void CmsSettings::UpdateServerConfig(const std::string &host, int port) {
        this->host_ = host;
        this->port_ = port;
    }

}
