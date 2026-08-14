//
// Created by RGAA on 26/08/2024.
//

#include "clickable_widget.h"
#include <QLinearGradient>

namespace tc
{

    ClickableWidget::ClickableWidget(QWidget* parent) : QWidget(parent) {

    }

    void ClickableWidget::SetOnClickListener(OnClickListener&& listener) {
        click_listener_ = listener;
    }

    void ClickableWidget::paintEvent(QPaintEvent *event) {
        QWidget::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setPen(Qt::NoPen);
        if (gr_from_color_ != 0 && gr_to_color_ != 0) {
            auto gradient = QLinearGradient(QPointF(0, 0), QPointF(this->width(), this->height()));
            gradient.setColorAt(0, gr_from_color_);
            gradient.setColorAt(1.0, gr_to_color_);
            painter.setBrush(QBrush(gradient));
        }
        else {
            if (pressed_) {
                painter.setBrush(QBrush(pressed_color_));
            } else if (enter_) {
                painter.setBrush(QBrush(enter_color_));
            } else {
                painter.setBrush(QBrush(normal_color_));
            }
        }

        int offset = 0;
        painter.drawRoundedRect(offset, offset, this->width() - 2 * offset, this->height() - 2 * offset, radius_, radius_);
    }

    void ClickableWidget::enterEvent(QEnterEvent *event) {
        enter_ = true;
        repaint();
    }

    void ClickableWidget::leaveEvent(QEvent *event) {
        enter_ = false;
        repaint();
    }

    void ClickableWidget::mousePressEvent(QMouseEvent *event) {
        pressed_ = true;
        repaint();
    }

    void ClickableWidget::mouseReleaseEvent(QMouseEvent *event) {
        pressed_ = false;
        repaint();
        if (click_listener_) {
            click_listener_(this);
        }
    }

    void ClickableWidget::SetColors(int normal_color, int enter_color, int pressed_color) {
        this->normal_color_ = normal_color;
        this->enter_color_ = enter_color;
        this->pressed_color_ = pressed_color;
        repaint();
    }

    void ClickableWidget::SetRadius(int radius) {
        this->radius_ = radius;
        repaint();
    }

    void ClickableWidget::SetGradientColor(int from_color, int to_color) {
        gr_from_color_ = from_color;
        gr_to_color_ = to_color;
        repaint();
    }
}