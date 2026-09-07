//
// Created by RGAA on 2023-08-18.
//

#ifndef ST_NETWORK_AUTO_JOIN_DIALOG_H
#define ST_NETWORK_AUTO_JOIN_DIALOG_H

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

#include "px_console_client/console_stream.h"
#include "px_qt_widget/px_custom_titlebar_dialog.h"

namespace px
{

    class PxContext;
    class PxApplication;
    class TcPasswordInput;
    class StNetworkConsoleAccessInfo;

    class StNetworkAutoJoinDialog : public TcCustomTitleBarDialog {
    public:
        StNetworkAutoJoinDialog(const std::shared_ptr<PxApplication>& app, const std::shared_ptr<StNetworkConsoleAccessInfo>& item, QWidget* parent = nullptr);
        ~StNetworkAutoJoinDialog() override;

        void paintEvent(QPaintEvent *event) override;
        void closeEvent(QCloseEvent *) override;

    private:
        void CreateLayout();

    private:
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        QLineEdit* edt_stream_name_ = nullptr;
        std::shared_ptr<StNetworkConsoleAccessInfo> item_ = nullptr;
        TcPasswordInput* password_input_ = nullptr;

    };

}

#endif //SAILFISH_CLIENT_PC_CREATESTREAMDIALOG_H
