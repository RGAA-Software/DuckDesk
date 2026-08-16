//
// Created by RGAA on 2023-08-18.
//

#include "input_safety_pwd_dialog.h"
#include <QValidator>
#include <QButtonGroup>
#include <QRadioButton>
#include <QTextEdit>
#include "px_dialog.h"
#include "px_label.h"
#include "px_pushbutton.h"
#include "px_cms_client/cms_stream.h"
#include "px_qt_widget/sized_msg_box.h"
#include "px_qt_widget/no_margin_layout.h"
#include "render_panel/px_application.h"
#include "render_panel/px_context.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/px_settings.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_common_new/http_client.h"
#include "px_qt_widget/px_password_input.h"
#include "px_cms_client/cms_device_api.h"
#include "px_cms_client/cms_device.h"

namespace px
{

    InputSafetyPwdDialog::InputSafetyPwdDialog(const std::shared_ptr<PxApplication>& app, QWidget* parent) : TcCustomTitleBarDialog("", parent) {
        app_ = app;
        context_ = app_->GetContext();
        setFixedSize(375, 300);
        CreateLayout();

        CenterDialog(this);
    }

    InputSafetyPwdDialog::~InputSafetyPwdDialog() = default;

    void InputSafetyPwdDialog::CreateLayout() {
        setWindowTitle(tcTr("id_input_security_password"));
        auto settings = PxSettings::Instance();

        auto item_width = 320;
        auto edit_size = QSize(item_width, 35);

        auto root_layout = new NoMarginHLayout();
        auto content_layout = new NoMarginVLayout();
        root_layout->addStretch();
        root_layout->addLayout(content_layout);
        root_layout->addStretch();
        root_layout_->addLayout(root_layout);

        content_layout->addSpacing(25);

        // password
        {
            auto layout = new NoMarginVLayout();

            auto label = new TcLabel(this);
            label->setFixedWidth(item_width);
            label->SetTextId("id_password");
            label->setStyleSheet(R"(color: #333333; font-weight: 700; font-size:13px;)");
            label->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            layout->addWidget(label);
            layout->addSpacing(10);

            pwd_input_ = new TcPasswordInput(this);
            pwd_input_->setFixedSize(edit_size);
            pwd_input_->SetPassword(settings->GetDeviceSecurityPwd().c_str());

            layout->addWidget(pwd_input_);
            layout->addStretch();

            content_layout->addLayout(layout);
        }

        content_layout->addSpacing(25);

        // password again
        {
            auto layout = new NoMarginVLayout();

            auto label = new TcLabel(this);
            label->setFixedWidth(item_width);
            label->SetTextId("id_password_again");
            label->setStyleSheet(R"(color: #333333; font-weight: 700; font-size:13px;)");
            label->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            layout->addWidget(label);
            layout->addSpacing(10);

            pwd_input_again_ = new TcPasswordInput(this);
            pwd_input_again_->setFixedSize(edit_size);
            pwd_input_again_->SetPassword(settings->GetDeviceSecurityPwd().c_str());
            layout->addWidget(pwd_input_again_);
            layout->addStretch();

            content_layout->addLayout(layout);
        }

        content_layout->addSpacing(50);

        // sure button
        {
            auto layout = new NoMarginVLayout();
            auto btn_sure = new TcPushButton();
            btn_sure->SetTextId("id_ok");
            connect(btn_sure, &QPushButton::clicked, this, [=, this] () {
                //if (settings->device_id_.empty()) {
                //    TcDialog warn_dialog(tcTr("id_warning"), tcTr("id_unmanaged_device"), this);
                //    warn_dialog.exec();
                //    return;
                //}

                auto pwd = pwd_input_->GetPassword();
                auto pwd_again = pwd_input_again_->GetPassword();
                if (pwd.isEmpty() || (pwd != pwd_again)) {
                    TcDialog warn_dialog(tcTr("id_warning"), tcTr("id_password_invalid_msg"), this);
                    warn_dialog.exec();
                    return;
                }

                // md5 password
                auto pwd_md5 = MD5::Hex(pwd.toStdString());

                // save to local db
                settings->SetDeviceSecurityPwd(pwd_md5);
                context_->NotifyAppMessage(tcTr("id_update_security_success"), tcTr("id_local_security_password_updated"));

                // update to renderer
                context_->SendAppMessage(MsgSecurityPasswordUpdated {
                    .security_password_ = pwd_md5,
                });

                // Supervisor server unconfigured
                if (settings->GetDeviceId().empty()) {
                    done(0);
                    return;
                }

                // update safety pwd
                auto opt_device = px_cms::CmsDeviceApi::UpdateSafetyPwd(settings->GetCmsServerHost(),
                                                                 settings->GetCmsServerPort(),
                                                                 grApp->GetAppkey(),
                                                                 settings->GetDeviceId(),
                                                                 pwd_md5);
                bool update_server_password_result = false;
                if (opt_device.has_value()) {
                    auto device = opt_device.value();
                    if (device->safety_pwd_md5_ == pwd_md5) {
                        update_server_password_result = true;
                        context_->NotifyAppMessage(tcTr("id_update_security_success"), tcTr("id_remote_security_password_updated"));
                        done(0);
                    }
                }

                if (!update_server_password_result) {
                    TcDialog warn_dialog(tcTr("id_warning"), tcTr("id_security_password_update_local_but_failed_server"), this);
                    warn_dialog.exec();
                }

            });

            layout->addWidget(btn_sure);
            btn_sure->setFixedSize(QSize(item_width, 35));
            content_layout->addLayout(layout);
        }

        content_layout->addSpacing(10);
        root_layout_->addStretch();
    }

    void InputSafetyPwdDialog::paintEvent(QPaintEvent *event) {
        TcCustomTitleBarDialog::paintEvent(event);
    }

    QString InputSafetyPwdDialog::GetInputPassword() {
        return pwd_input_->GetPassword();
    }

    void InputSafetyPwdDialog::closeEvent(QCloseEvent* ) {
        done(1);
    }

}