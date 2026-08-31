//
// Created by RGAA on 2023-08-18.
//

#ifndef SAILFISH_CLIENT_PC_INPUT_SAFETY_PWD_DIALOG_H
#define SAILFISH_CLIENT_PC_INPUT_SAFETY_PWD_DIALOG_H

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
#include <QTextEdit>
#include <QPointer>
#include <functional>
#include "px_qt_widget/px_custom_titlebar_dialog.h"

namespace px
{

    class PxContext;
    class PxApplication;
    class TcPasswordInput;
    class PxSettings;
    class LatestSerialRequestGate;

    class InputSafetyPwdDialog : public TcCustomTitleBarDialog {
    public:
        explicit InputSafetyPwdDialog(const std::shared_ptr<PxApplication>& ctx, QWidget* parent = nullptr);
        ~InputSafetyPwdDialog() override;
        void paintEvent(QPaintEvent *event) override;
        void closeEvent(QCloseEvent *) override;
        QString GetInputPassword();

    private:
        void CreateLayout();

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::reference_wrapper<PxSettings> settings_;
        QPointer<TcPasswordInput> pwd_input_;
        QPointer<TcPasswordInput> pwd_input_again_;
        std::shared_ptr<LatestSerialRequestGate> update_gate_;

    };

}

#endif
