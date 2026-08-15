//
// Created by RGAA on 29/03/2025.
//

#include "tc_password_input.h"
#include "tc_image_button.h"

namespace tc
{

    TcPasswordInput::TcPasswordInput(QWidget* parent) : QWidget(parent) {
        auto remote_pwd = new QLineEdit(this);
        remote_password_ = remote_pwd;
        remote_pwd->setEchoMode(QLineEdit::Password);
        remote_pwd->setText("");
        remote_pwd->setStyleSheet(R"(font-size: 16px; font-weight: 700; color: #2979ff;)");

        auto show_password = new TcImageButton(":/resources/image/ic_pwd_visibility_on.svg", ":/resources/image/ic_pwd_visibility_off.svg", QSize(16, 16), this);
        btn_password_echo_change_ = show_password;
        show_password->SetColor(0xf5f5f5, 0xe9e9e9, 0xd8d8d8);
        show_password->SetRoundRadius(15);
        show_password->setFixedSize(22, 22);
        show_password->SetOnImageButtonClicked([=, this]() {
            if (remote_pwd->echoMode() == QLineEdit::Password) {
                remote_pwd->setEchoMode(QLineEdit::Normal);
                show_password->ToImage1();
            }
            else {
                remote_pwd->setEchoMode(QLineEdit::Password);
                show_password->ToImage2();
            }
        });
        show_password->ToImage2();
    }

    void TcPasswordInput::resizeEvent(QResizeEvent *event) {
        auto btn_width = btn_password_echo_change_->width();
        auto btn_height = btn_password_echo_change_->height();
        remote_password_->setGeometry(this->rect());
        btn_password_echo_change_->setGeometry(this->width() - 5 - btn_width, (this->height()-btn_height)/2, btn_width, btn_height);
    }

    void TcPasswordInput::SetPassword(const QString& password) {
        remote_password_->setText(password);
    }

    QString TcPasswordInput::GetPassword() {
        return remote_password_->text();
    }
}