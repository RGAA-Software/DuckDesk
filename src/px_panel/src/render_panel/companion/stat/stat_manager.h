//
// Created by RGAA on 8/02/2026.
//

#ifndef GAMMARAYPREMIUM_STAT_MANAGER_H
#define GAMMARAYPREMIUM_STAT_MANAGER_H

#include <memory>
#include <string>

namespace px
{
    class SysInfo;
    class PanelCompanionImpl;

    class StatManager {
    public:
        explicit StatManager(PanelCompanionImpl* impl);
        // report auth
        [[nodiscard]] bool ReportWorkingAuth(const std::shared_ptr<SysInfo>& info);

        // report software open up
        [[nodiscard]] bool ReportOpenUp(const std::shared_ptr<SysInfo>& info);

    private:
        PanelCompanionImpl* impl_ = nullptr;
    };

}

#endif //GAMMARAYPREMIUM_STAT_MANAGER_H