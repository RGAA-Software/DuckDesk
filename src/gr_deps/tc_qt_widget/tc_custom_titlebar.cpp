//
// Created by RGAA on 23/03/2025.
//

#include "tc_custom_titlebar.h"

#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include "tc_label.h"
#include "widget_helper.h"

namespace tc
{

    OperationIcon::OperationIcon(int radius, int normal_color, int enter_color, QWidget *parent) : QWidget(parent) {
        this->radius_ = radius;
        this->normal_color_ = normal_color;
        this->enter_color_ = enter_color;
    }

    void OperationIcon::paintEvent(QPaintEvent *event) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::RenderHint::Antialiasing);
        painter.setPen(Qt::NoPen);
        if (enter_) {
            painter.setBrush(QBrush(QColor(this->enter_color_)));
        } else {
            painter.setBrush(QBrush(QColor(this->normal_color_)));
        }
        painter.drawRoundedRect(this->rect(), this->width() / 2, this->height() / 2);
    }

    void OperationIcon::enterEvent(QEnterEvent *event) {
        enter_ = true;
        repaint();
    }

    void OperationIcon::leaveEvent(QEvent *event) {
        enter_ = false;
        repaint();
    }

    void OperationIcon::mousePressEvent(QMouseEvent *event) {
        pressed_ = true;
        repaint();
    }

    void OperationIcon::mouseReleaseEvent(QMouseEvent *event) {
        pressed_ = false;
        repaint();

        if (click_cbk_) {
            click_cbk_();
        }
    }

    void OperationIcon::SetOnClickCallback(ClickCallback &&cbk) {
        click_cbk_ = std::move(cbk);
    }

///// ------------------------------------------------------////

    TcCustomTitleBar::TcCustomTitleBar(const QString& title, QWidget *parent) : QWidget(parent) {
        setFixedHeight(30);
        auto root_layout = new QVBoxLayout();
        WidgetHelper::ClearMargins(root_layout);

        auto item_layout = new QHBoxLayout();
        WidgetHelper::ClearMargins(item_layout);
        item_layout->addSpacing(10);

        auto btn_size = 14;

        // icon
        {
            auto lbl_icon = new TcLabel(this);
            lbl_icon->setFixedSize(16, 16);
            lbl_icon->setStyleSheet(R"( background-image: url(:resources/image/ic_info.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");

            item_layout->addWidget(lbl_icon);
        }

        item_layout->addSpacing(10);

        // title
        {
            auto lbl_title = new TcLabel(this);
            lbl_title->setText(title);
            lbl_title->setStyleSheet("font-weight: 700;");
            item_layout->addWidget(lbl_title);
        }

        item_layout->addStretch();

        // min
//        {
//            min_btn_ = new OperationIcon(btn_size,
//                                         Settings::kOperationBtnMinNomralColor,
//                                         Settings::kOperationBtnMinEnterColor,
//                                         this);
//            min_btn_->setFixedSize(QSize(btn_size, btn_size));
//            item_layout->addWidget(min_btn_);
//        }
//        item_layout->addSpacing(6);

        // close
        {
            close_btn_ = new OperationIcon(btn_size,
                                           0xdc3545,
                                           0xcb2535,
                                           this);
            close_btn_->setFixedSize(QSize(btn_size, btn_size));
            item_layout->addWidget(close_btn_);
        }
        item_layout->addSpacing(10);
        root_layout->addLayout(item_layout);
        setLayout(root_layout);
    }

    TcCustomTitleBar::~TcCustomTitleBar() {

    }

    void TcCustomTitleBar::paintEvent(QPaintEvent *event) {
#if 0
        QPainter painter(this);
        painter.setBrush(QBrush(0x0099ff));
        painter.drawRect(this->rect());
#endif
    }

    void TcCustomTitleBar::SetMinClickCallback(ClickCallback &&cbk) {
        min_btn_->SetOnClickCallback(std::move(cbk));
    }

    void TcCustomTitleBar::SetCloseClickCallback(ClickCallback &&cbk) {
        close_btn_->SetOnClickCallback(std::move(cbk));
    }

    void TcCustomTitleBar::mousePressEvent(QMouseEvent *event) {
        if (event->buttons() & Qt::LeftButton) {
            click_point_ = event->globalPos();
            click_window_pos_ = ((QWidget *) this->parent())->pos();
            left_pressed_ = true;
        }
    }

    void TcCustomTitleBar::mouseMoveEvent(QMouseEvent *event) {
        if ((event->buttons() & Qt::LeftButton) && left_pressed_) {
            QPoint point_offset = event->globalPos() - click_point_;
            auto target_pos = click_window_pos_ + point_offset;
            ((QWidget *) this->parent())->move(target_pos);
        }
    }

    void TcCustomTitleBar::mouseReleaseEvent(QMouseEvent *event) {
        left_pressed_ = false;
    }

}