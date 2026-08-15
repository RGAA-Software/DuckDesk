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

#include "px_cms_client/cms_stream.h"
#include "px_qt_widget/px_custom_titlebar_dialog.h"

namespace px
{

    class GrContext;
    class GrApplication;
    class TcPasswordInput;
    class StNetworkCmsAccessInfo;

    class StNetworkAutoJoinDialog : public TcCustomTitleBarDialog {
    public:
        StNetworkAutoJoinDialog(const std::shared_ptr<GrApplication>& app, const std::shared_ptr<StNetworkCmsAccessInfo>& item, QWidget* parent = nullptr);
        ~StNetworkAutoJoinDialog() override;

        void paintEvent(QPaintEvent *event) override;
        void closeEvent(QCloseEvent *) override;

    private:
        void CreateLayout();

    private:
        std::shared_ptr<GrApplication> app_ = nullptr;
        std::shared_ptr<GrContext> context_ = nullptr;
        QLineEdit* edt_stream_name_ = nullptr;
        std::shared_ptr<StNetworkCmsAccessInfo> item_ = nullptr;
        TcPasswordInput* password_input_ = nullptr;

    };

}

#endif //SAILFISH_CLIENT_PC_CREATESTREAMDIALOG_H
