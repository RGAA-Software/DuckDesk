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

    class ModifyPasswordDialog : public TcCustomTitleBarDialog {
    public:
        explicit ModifyPasswordDialog(const std::shared_ptr<PxContext>& ctx, QWidget* parent = nullptr);
        ~ModifyPasswordDialog() override;

        std::string GetCurrentPassword();
        std::string GetNewPassword();
        std::string GetNewPasswordAgain();

        void paintEvent(QPaintEvent *event) override;

    private:
        void CreateLayout();
        void ModifyPassword();

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        TcPasswordInput* current_password_input_ = nullptr;
        TcPasswordInput* new_password_input_ = nullptr;
        TcPasswordInput* new_password_input_again_ = nullptr;

    };

}
