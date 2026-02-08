//
// Created by RGAA on 8/02/2026.
//

#include "stat_manager.h"

namespace tc
{

    StatManager::StatManager(PanelCompanionImpl *impl) {
        impl_ = impl;
    }

    bool StatManager::ReportWorkingAuth(const std::shared_ptr<SysInfo>& info) {
        return true;
    }

}
