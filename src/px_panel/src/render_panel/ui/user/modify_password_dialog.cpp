//
// Created by RGAA on 2023-08-18.
//

#include "modify_password_dialog.h"
#include <QValidator>
#include <QButtonGroup>
#include <QRadioButton>
#include "px_qt_widget/sized_msg_box.h"
#include "px_qt_widget/no_margin_layout.h"
#include "px_qt_widget/px_password_input.h"
#include "px_dialog.h"
#include "px_label.h"
#include "px_pushbutton.h"
#include "render_panel/px_context.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/px_application.h"
#include "render_panel/user/px_user_manager.h"
#include "px_common_new/log.h"

namespace px
{

    ModifyPasswordDialog::ModifyPasswordDialog(const std::shared_ptr<PxContext>& ctx, QWidget* parent) : TcCustomTitleBarDialog("", parent) {
        context_ = ctx;
        setFixedSize(375, 420);
        CreateLayout();
    }

    ModifyPasswordDialog::~ModifyPasswordDialog() = default;

    void ModifyPasswordDialog::CreateLayout() {
        setWindowTitle(tcTr("id_edit_password"));

        auto item_width = 320;
        auto edit_size = QSize(item_width, 35);

        auto root_layout = new NoMarginHLayout();
        auto content_layout = new NoMarginVLayout();
        root_layout->addStretch();
        root_layout->addLayout(content_layout);
        root_layout->addStretch();
        root_layout_->addLayout(root_layout);

        content_layout->addSpacing(25);

        // 1. current password
        {
            auto layout = new NoMarginVLayout();
            auto label = new TcLabel(this);
            label->setFixedWidth(item_width);
            label->setStyleSheet(R"(color: #333333; font-weight: 700; font-size:13px;)");
            label->SetTextId("id_current_password");
            label->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            layout->addWidget(label);
            layout->addSpacing(10);

            auto edit = new TcPasswordInput(this);
            current_password_input_ = edit;
            current_password_input_->SetPassword("");

            edit->setFixedSize(edit_size);
            layout->addWidget(edit);
            layout->addStretch();
            content_layout->addLayout(layout);
        }

        content_layout->addSpacing(25);

        // 2. new password
        {
            auto layout = new NoMarginVLayout();
            auto label = new TcLabel(this);
            label->setFixedWidth(item_width);
            label->setStyleSheet(R"(color: #333333; font-weight: 700; font-size:13px;)");
            label->SetTextId("id_new_password");
            label->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            layout->addWidget(label);
            layout->addSpacing(10);

            auto edit = new TcPasswordInput(this);
            new_password_input_ = edit;
            new_password_input_->SetPassword("");

            edit->setFixedSize(edit_size);
            layout->addWidget(edit);
            layout->addStretch();
            content_layout->addLayout(layout);
        }

        content_layout->addSpacing(25);

        // 3. repeat new password
        {
            auto layout = new NoMarginVLayout();
            auto label = new TcLabel(this);
            label->setFixedWidth(item_width);
            label->setStyleSheet(R"(color: #333333; font-weight: 700; font-size:13px;)");
            label->SetTextId("id_repeat_new_password");
            label->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            layout->addWidget(label);
            layout->addSpacing(10);

            auto edit = new TcPasswordInput(this);
            new_password_input_again_ = edit;
            new_password_input_again_->SetPassword("");
            edit->setFixedSize(edit_size);
            layout->addWidget(edit);
            layout->addStretch();
            content_layout->addLayout(layout);
        }

        content_layout->addStretch();

        // sure button
        {
            auto layout = new NoMarginVLayout();
            auto btn_sure = new TcPushButton();
            btn_sure->SetTextId("id_ok");
            connect(btn_sure, &QPushButton::clicked, this, [=, this] () {
                ModifyPassword();
            });

            layout->addWidget(btn_sure);
            btn_sure->setFixedSize(QSize(item_width, 35));

            content_layout->addLayout(layout);
        }
        content_layout->addSpacing(35);
    }

    void ModifyPasswordDialog::paintEvent(QPaintEvent *event) {
        TcCustomTitleBarDialog::paintEvent(event);
    }

    std::string ModifyPasswordDialog::GetCurrentPassword() {
        return current_password_input_->GetPassword().toStdString();
    }

    std::string ModifyPasswordDialog::GetNewPassword() {
        return new_password_input_->GetPassword().toStdString();
    }

    std::string ModifyPasswordDialog::GetNewPasswordAgain() {
        return new_password_input_again_->GetPassword().toStdString();
    }

    void ModifyPasswordDialog::ModifyPassword() {
        auto user_mgr = grApp->GetUserManager();
        auto current_password = GetCurrentPassword();
        auto new_password = GetNewPassword();
        auto new_password_again = GetNewPasswordAgain();
        if (current_password.empty() || new_password.empty() || new_password_again.empty()) {
            return;
        }
        if (new_password != new_password_again) {
            TcDialog dialog(tcTr("id_error"), tcTr("id_password_invalid"));
            dialog.exec();
            return;
        }
        bool r = user_mgr->ModifyPassword(current_password, new_password);
        if (r) {
            done(kDoneOk);
        }
        else {
            LOGE("ModifyPassword failed");
        }
    }

}
