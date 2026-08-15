//
// Created by RGAA on 13/11/2025.
//

#include "tc_circle_indicator.h"

namespace tc
{

    TcCircleIndicator::TcCircleIndicator(QWidget* parent) : QWidget(parent) {

    }

    TcCircleIndicator::~TcCircleIndicator() = default;

    void TcCircleIndicator::paintEvent(QPaintEvent *event) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(0x777777);
        painter.setPen(pen);
        int color = 0x999999;
        if (state_ == State::kOk) {
            color = ok_color_;
        }
        else if (state_ == State::kError) {
            color = err_color_;
        }
        else if (state_ == State::kWarn) {
            color = warn_color_;
        }
        painter.setBrush(QBrush(color));
        painter.drawRoundedRect(1, 1, this->width()-2, this->height()-2, this->width()/2.0f, this->height()/2.0f);
    }

    void TcCircleIndicator::SetState(State st) {
        state_ = st;
        update();
    }

}