//
// Created by RGAA on 2024-06-10.
//

#ifndef PX_ST_ABOUT_ME_H
#define PX_ST_ABOUT_ME_H

#include <QLabel>
#include "tab_base.h"

namespace px
{
    class PxApplication;

    class StAboutMe : public TabBase {
    public:
        explicit StAboutMe(const std::shared_ptr<PxApplication>& app, QWidget* parent = nullptr);
        ~StAboutMe() override = default;

        void OnTabShow() override;
        void OnTabHide() override;

    private:
        QLabel* license_ = nullptr;
    };

}

#endif //PX_ST_ABOUT_ME_H
