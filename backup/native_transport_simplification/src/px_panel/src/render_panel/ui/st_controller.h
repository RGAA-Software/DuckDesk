//
// Created by RGAA on 2024-06-10.
//

#ifndef PX_ST_CONTROLLER_H
#define PX_ST_CONTROLLER_H

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include "tab_base.h"

namespace px
{
    class PxSettings;
    class PxApplication;

    class StController : public TabBase {
    public:
        explicit StController(const std::shared_ptr<PxApplication>& app, QWidget* parent = nullptr);
        ~StController() override = default;

        void OnTabShow() override;
        void OnTabHide() override;

    private:
        PxSettings* settings_ = nullptr;
        QLineEdit* et_screen_recording_path_ = nullptr;
        QPushButton* btn_select_screen_recording_path_ = nullptr;
    };

}

#endif //PX_ST_ABOUT_ME_H
