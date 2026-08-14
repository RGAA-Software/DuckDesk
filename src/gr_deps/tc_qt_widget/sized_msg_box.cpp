//
// Created by RGAA  on 2024/1/17.
//

#include "sized_msg_box.h"
#include "no_margin_layout.h"
#include "tc_qt_widget/tc_pushbutton.h"
#include "tc_qt_widget/widget_helper.h"
#include "tc_qt_widget/tc_custom_titlebar.h"
#include <QLabel>
#include <QPushButton>
#include <QPainter>

namespace tc
{

    std::shared_ptr<SizedMessageBox> SizedMessageBox::MakeOkBox(const QString& title, const QString& msg) {
        auto msg_box = std::make_shared<SizedMessageBox>(title, true, false);
        msg_box->setFixedSize(410, 220);
        msg_box->setWindowTitle(title);
        msg_box->lbl_message_->setText(msg);

        connect(msg_box->btn_ok_, &QPushButton::clicked, msg_box.get(), [=]() {
            msg_box->done(0);
        });
        return msg_box;
    }

    std::shared_ptr<SizedMessageBox> SizedMessageBox::MakeOkCancelBox(const QString& title, const QString& msg) {
        auto msg_box = std::make_shared<SizedMessageBox>(title, true, true);
        msg_box->setFixedSize(410, 220);
        msg_box->setWindowTitle(title);
        msg_box->lbl_message_->setText(msg);

        connect(msg_box->btn_ok_, &QPushButton::clicked, msg_box.get(), [=]() {
            msg_box->done(0);
        });
        connect(msg_box->btn_cancel_, &QPushButton::clicked, msg_box.get(), [=]() {
            msg_box->done(1);
        });
        return msg_box;
    }

    std::shared_ptr<SizedMessageBox> SizedMessageBox::Make2BtnsBox(const QString& title, const QString& msg,
                                                         const QString& btn_left_text, const QString& btn_right_text) {
        auto msg_box = std::make_shared<SizedMessageBox>(title, true, true);
        msg_box->setFixedSize(410, 220);
        msg_box->setWindowTitle(title);
        msg_box->lbl_message_->setText(msg);
        msg_box->btn_ok_->setText(btn_right_text);
        connect(msg_box->btn_ok_, &QPushButton::clicked, msg_box.get(), [=]() {
            msg_box->done(0);
        });
        msg_box->btn_cancel_->setText(btn_left_text);
        connect(msg_box->btn_cancel_, &QPushButton::clicked, msg_box.get(), [=]() {
            msg_box->done(1);
        });
        return msg_box;
    }

    std::shared_ptr<SizedMessageBox> SizedMessageBox::MakeErrorOkBox(const QString& title, const QString& msg) {
        auto box = MakeOkBox(title, msg);
        box->setWindowTitle(title);
        box->setWindowIcon(QIcon(":/resources/ic_error.png"));
        return box;
    }

    std::shared_ptr<SizedMessageBox> SizedMessageBox::MakeInfoOkBox(const QString& title, const QString& msg) {
        auto box = MakeOkBox(title, msg);
        box->setWindowTitle(title);
        box->setWindowIcon(QIcon(":/resources/ic_info.png"));
        return box;
    }

    SizedMessageBox::SizedMessageBox(const QString& title, bool ok, bool cancel, QWidget* parent) {
        if (false) {
            setWindowFlags(Qt::FramelessWindowHint);
            setAttribute(Qt::WA_TranslucentBackground);
        }

        auto layout = new NoMarginVLayout();

        // title bar
        if (false) {
            auto titlebar = new TcCustomTitleBar(title, this);
            title_bar_ = titlebar;
            layout->addSpacing(shadow_offset + titlebar->height());

            titlebar->SetCloseClickCallback([this]() {
                this->done(1);
            });
        }

        layout->addSpacing(20);
        {
            auto item_layout = new NoMarginHLayout();
            auto lbl_message = new QLabel(this);
            lbl_message->setWordWrap(true);
            lbl_message->setStyleSheet("font-size: 15px;");
            lbl_message_ = lbl_message;
            item_layout->addSpacing(20);
            item_layout->addWidget(lbl_message);
            item_layout->addSpacing(20);
            layout->addLayout(item_layout);
        }
        layout->addStretch();
        {
            auto item_layout = new NoMarginHLayout();
            auto btn_size = QSize(90, 28);
            item_layout->addStretch();
            if (cancel) {
                auto btn_cancel = new TcPushButton(this);
                btn_cancel->SetTextId("id_cancel");
                btn_cancel->setProperty("class", "danger");
                btn_cancel_ = btn_cancel;
                btn_cancel->setFixedSize(btn_size);
                item_layout->addWidget(btn_cancel);
                item_layout->addSpacing(20);
            }

            if (ok) {
                auto btn_ok = new TcPushButton(this);
                btn_ok->SetTextId("id_ok");
                btn_ok_ = btn_ok;
                btn_ok->setFixedSize(btn_size);
                item_layout->addWidget(btn_ok);
                item_layout->addStretch();
            }

            layout->addLayout(item_layout);
        }
        layout->addSpacing(20);
        setLayout(layout);

        WidgetHelper::AddShadow(this, 0x777777, shadow_offset);
    }

    void SizedMessageBox::paintEvent(QPaintEvent *event) {
        if (title_bar_) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::RenderHint::Antialiasing);
            int radius = 4;
            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush(QColor(0xffffff)));
            int padding = shadow_offset;
            QRect inner_rect(padding, padding, this->rect().width() - padding * 2, this->rect().height() - padding * 2);
            painter.drawRoundedRect(inner_rect, radius, radius);
        }
        else {
            QDialog::paintEvent(event);
        }
    }

    void SizedMessageBox::resizeEvent(QResizeEvent* evt) {
        if (title_bar_) {
            auto title_bar_offset = shadow_offset + 2;
            title_bar_->setGeometry(title_bar_offset, title_bar_offset, evt->size().width() - 2 * title_bar_offset,
                                    title_bar_->height());
        }
    }

}
