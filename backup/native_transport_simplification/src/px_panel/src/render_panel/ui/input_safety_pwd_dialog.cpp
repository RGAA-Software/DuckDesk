//
// Created by RGAA on 2023-08-18.
//

#include "input_safety_pwd_dialog.h"
#include <QValidator>
#include <QButtonGroup>
#include <QRadioButton>
#include <QTextEdit>
#include <optional>
#include "px_dialog.h"
#include "px_label.h"
#include "px_pushbutton.h"
#include "px_console_client/console_stream.h"
#include "px_qt_widget/sized_msg_box.h"
#include "px_qt_widget/no_margin_layout.h"
#include "render_panel/px_application.h"
#include "render_panel/px_context.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/px_settings.h"
#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_common/http_client.h"
#include "px_qt_widget/px_password_input.h"
#include "px_console_client/console_device_api.h"
#include "px_console_client/console_device.h"
#include "px_common/latest_serial_request_gate.h"

namespace px
{

    InputSafetyPwdDialog::InputSafetyPwdDialog(
        const std::shared_ptr<PxApplication>& app,
        QWidget* parent) // NOLINT(gammaray-raw-pointer-boundary) Qt parent API
        : TcCustomTitleBarDialog("", parent),
          context_(app->GetContext()),
          app_(app),
          settings_(*PxSettings::Instance()),
          update_gate_(LatestSerialRequestGate::Create()) {
        setFixedSize(375, 300);
        CreateLayout();

        CenterDialog(this);
    }

    InputSafetyPwdDialog::~InputSafetyPwdDialog() {
        update_gate_->Stop();
    }

    void InputSafetyPwdDialog::CreateLayout() {
        setWindowTitle(tcTr("id_input_security_password"));
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
            pwd_input_->SetPassword(
                settings_.get().GetDeviceSecurityPwd().c_str());

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
            pwd_input_again_->SetPassword(
                settings_.get().GetDeviceSecurityPwd().c_str());
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
            const QPointer<InputSafetyPwdDialog> self(this);
            connect(btn_sure, &QPushButton::clicked, this, [self]() {
                if (!self || !self->pwd_input_ || !self->pwd_input_again_) {
                    return;
                }
                //if (settings->device_id_.empty()) {
                //    TcDialog warn_dialog(tcTr("id_warning"), tcTr("id_unmanaged_device"), this);
                //    warn_dialog.exec();
                //    return;
                //}

                auto pwd = self->pwd_input_->GetPassword();
                auto pwd_again = self->pwd_input_again_->GetPassword();
                if (pwd.isEmpty() || (pwd != pwd_again)) {
                    TcDialog warn_dialog(
                        tcTr("id_warning"),
                        tcTr("id_password_invalid_msg"),
                        self);
                    warn_dialog.exec();
                    return;
                }

                // md5 password
                auto pwd_md5 = MD5::Hex(pwd.toStdString());

                // save to local db
                auto& settings = self->settings_.get();
                settings.SetDeviceSecurityPwd(pwd_md5);
                self->context_->NotifyAppMessage(
                    tcTr("id_update_security_success"),
                    tcTr("id_local_security_password_updated"));

                // update to renderer
                self->context_->SendAppMessage(MsgSecurityPasswordUpdated {
                    .security_password_ = pwd_md5,
                });

                // Supervisor server unconfigured
                if (settings.GetDeviceId().empty()) {
                    self->done(0);
                    return;
                }
                const auto request = self->update_gate_->Begin();
                if (!request) {
                    return;
                }
                const auto gate = self->update_gate_;
                const auto context = self->context_;
                const auto application = self->app_;
                const auto host = settings.GetConsoleServerHost();
                const auto port = settings.GetConsoleServerPort();
                const auto device_id = settings.GetDeviceId();
                context->PostNetworkTask([
                    self, gate, request, context, application, host, port,
                    device_id, pwd_md5]() {
                    std::optional<Result<
                        std::shared_ptr<px_console::ConsoleDevice>,
                        px_console::ConsoleApiError>> result;
                    static_cast<void>(gate->RunIfCurrent(
                        request.generation,
                        [&result, &request, &application, &host, &port,
                         &device_id, &pwd_md5]() {
                            result.emplace(
                                px_console::ConsoleDeviceApi::UpdateSafetyPwd(
                                    host,
                                    port,
                                    application->GetAppkey(),
                                    device_id,
                                    pwd_md5,
                                    request.cancellation));
                        }));
                    context->PostUITask([
                        self, gate, generation = request.generation,
                        pwd_md5, result = std::move(result)]() mutable {
                        if (!self || !gate->Complete(generation) || !result) {
                            return;
                        }
                        if (result->has_value() && result->value()
                            && result->value()->safety_pwd_md5_ == pwd_md5) {
                            self->context_->NotifyAppMessage(
                                tcTr("id_update_security_success"),
                                tcTr("id_remote_security_password_updated"));
                            self->done(0);
                            return;
                        }
                        TcDialog warn_dialog(
                            tcTr("id_warning"),
                            tcTr("id_security_password_update_local_but_failed_server"),
                            self);
                        warn_dialog.exec();
                    });
                });
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
