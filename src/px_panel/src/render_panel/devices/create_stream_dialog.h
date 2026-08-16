//
// Created by RGAA on 2023-08-18.
//

#ifndef SAILFISH_CLIENT_PC_CREATESTREAMDIALOG_H
#define SAILFISH_CLIENT_PC_CREATESTREAMDIALOG_H

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

    class PxContext;

    // Create with host & port
    class CreateStreamDialog : public TcCustomTitleBarDialog {
    public:
        explicit CreateStreamDialog(const std::shared_ptr<PxContext>& ctx, QWidget* parent = nullptr);
        CreateStreamDialog(const std::shared_ptr<PxContext>& ctx, const std::shared_ptr<px_cms::CmsStream>& item, QWidget* parent = nullptr);
        ~CreateStreamDialog() override;

        void paintEvent(QPaintEvent *event) override;

    private:
        void CreateLayout();
        bool GenStream();

    private:
        std::shared_ptr<PxContext> context_ = nullptr;

        QLineEdit* ed_name_ = nullptr;
        QLineEdit* ed_host_ = nullptr;
        QLineEdit* ed_port_ = nullptr;
        QLineEdit* ed_bitrate_ = nullptr;
        QLineEdit* ed_remote_device_id_ = nullptr;
        QComboBox* cb_fps_ = nullptr;
        QRadioButton* rb_ws_ = nullptr;
        //QRadioButton* rb_udp_ = nullptr;
        QRadioButton* rb_relay_ = nullptr;
        std::shared_ptr<px_cms::CmsStream> stream_item_;

    };

}

#endif //SAILFISH_CLIENT_PC_CREATESTREAMDIALOG_H
