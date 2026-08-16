//
// Created by RGAA on 2023-08-18.
//

#ifndef SAILFISH_CLIENT_PC_EDIT_RELAY_STREAM_DIALOG_H
#define SAILFISH_CLIENT_PC_EDIT_RELAY_STREAM_DIALOG_H

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
    class TcPasswordInput;

    class EditRelayStreamDialog : public TcCustomTitleBarDialog {
    public:
        EditRelayStreamDialog(const std::shared_ptr<PxContext>& ctx, const std::shared_ptr<px_cms::CmsStream>& item, QWidget* parent = nullptr);
        ~EditRelayStreamDialog() override;

        void paintEvent(QPaintEvent *event) override;

    private:
        void CreateLayout();

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        QLineEdit* edt_stream_name_ = nullptr;
        std::shared_ptr<px_cms::CmsStream> stream_item_;
        TcPasswordInput* password_input_ = nullptr;
        QLineEdit* ed_host_ = nullptr;
        QLineEdit* ed_port_ = nullptr;

    };

}

#endif //SAILFISH_CLIENT_PC_CREATESTREAMDIALOG_H
