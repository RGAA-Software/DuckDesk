//
// Created by RGAA on 26/04/2025.
//

#include "tc_base_widget.h"

namespace tc
{

    TcBaseWidget::TcBaseWidget(QWidget* parent) : QWidget(parent) {

    }

    void TcBaseWidget::enterEvent(QEnterEvent *event) {
        QWidget::enterEvent(event);
        mouse_enter_ = true;
        if (enter_cbk_) {
            enter_cbk_(this, true);
        }
    }

    void TcBaseWidget::leaveEvent(QEvent *event) {
        QWidget::leaveEvent(event);
        mouse_enter_ = false;
        if (enter_cbk_) {
            enter_cbk_(this, false);
        }
    }

    void TcBaseWidget::SetOnEnterWidgetCallback(OnEnterWidgetCallback&& cbk) {
        enter_cbk_ = cbk;
    }

    void TcBaseWidget::SetOnClickWidgetCallback(OnClickWidgetCallback&& cbk) {
        click_cbk_ = cbk;
    }

    void TcBaseWidget::mousePressEvent(QMouseEvent *event) {
        QWidget::mousePressEvent(event);
    }

    void TcBaseWidget::mouseReleaseEvent(QMouseEvent *event) {
        QWidget::mouseReleaseEvent(event);
        if (click_cbk_) {
            click_cbk_(this);
        }
    }

}