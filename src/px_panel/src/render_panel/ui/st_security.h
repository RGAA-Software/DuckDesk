//
// Created by RGAA on 2024-06-10.
//

#ifndef PX_ST_SECURITY_H
#define PX_ST_SECURITY_H

#include <functional>

#include <QLabel>
#include "tab_base.h"

namespace px
{
    class PxSettings;
    class PxApplication;

    class StSecurity : public TabBase {
    public:
        explicit StSecurity(const std::shared_ptr<PxApplication>& app, QWidget* parent = nullptr);
        ~StSecurity() override = default;

        void OnTabShow() override;
        void OnTabHide() override;

    private:
        std::reference_wrapper<PxSettings> security_settings_;
    };

}

#endif //PX_ST_ABOUT_ME_H
