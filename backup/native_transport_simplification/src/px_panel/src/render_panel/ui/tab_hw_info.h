//
// Created by RGAA on 22/03/2025.
//

#ifndef PX_TAB_HW_INFO_INTERNALS_H
#define PX_TAB_HW_INFO_INTERNALS_H

#include <map>
#include <QStackedWidget>
#include <QPushButton>
#include "tab_base.h"

namespace px
{

    class HWInfoWidget;

    class TabHWInfo : public TabBase {
    public:
        TabHWInfo(const std::shared_ptr<PxApplication>& app, QWidget *parent);

        void OnTabShow() override;
        void OnTabHide() override;

    private:
        HWInfoWidget* hw_widget_ = nullptr;
    };

}

#endif //PX_TAB_PROFILE_H
