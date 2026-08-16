//
// Created by RGAA on 2023-08-18.
//

#ifndef SAILFISH_CLIENT_PC_CREATESTREAM_CONN_INFO_DIALOG_H
#define SAILFISH_CLIENT_PC_CREATESTREAM_CONN_INFO_DIALOG_H

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

#include "px_qt_widget/px_custom_titlebar_dialog.h"

namespace px
{

    class PxContext;
    class TcPasswordInput;

    class InputRemotePwdDialog : public TcCustomTitleBarDialog {
    public:
        explicit InputRemotePwdDialog(const std::shared_ptr<PxContext>& ctx, QWidget* parent = nullptr);
        ~InputRemotePwdDialog() override;
        void paintEvent(QPaintEvent *event) override;
        void closeEvent(QCloseEvent *) override;
        QString GetInputPassword();

    private:
        void CreateLayout();

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        TcPasswordInput* pwd_input_ = nullptr;

    };

}

#endif
