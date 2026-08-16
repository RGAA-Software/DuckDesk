//
// Created by RGAA on 2023-08-18.
//

#pragma once

#include <QDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QRadioButton>
#include <QComboBox>
#include <QPainter>
#include "px_qt_widget/px_custom_titlebar_dialog.h"

namespace px
{

    class CmsUser;
    class PxContext;
    class TcPasswordInput;

    class ModifyUsernameDialog : public TcCustomTitleBarDialog {
    public:
        ModifyUsernameDialog(const std::shared_ptr<PxContext>& ctx, QWidget* parent = nullptr);
        ~ModifyUsernameDialog() override;

        std::string GetUsername();

        void paintEvent(QPaintEvent *event) override;

    private:
        void CreateLayout();
        void ModifyUsername();

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        QLineEdit* edt_username_ = nullptr;

    };

}
