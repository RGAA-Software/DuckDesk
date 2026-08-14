//
// Created by RGAA on 26/08/2024.
//

#ifndef GAMMARAYPC_CLICKABLE_WIDGET_H
#define GAMMARAYPC_CLICKABLE_WIDGET_H

#include <QWidget>
#include <QPainter>

#include "click_listener.h"

namespace tc
{

    class ClickableWidget : public QWidget {
    public:
        explicit ClickableWidget(QWidget* parent = nullptr);

        void paintEvent(QPaintEvent *event) override;
        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;

        void SetOnClickListener(OnClickListener&& listener);
        void SetColors(int normal_color, int enter_color, int pressed_color);
        void SetGradientColor(int from_color, int to_color);
        void SetRadius(int radius);
    protected:
        OnClickListener click_listener_;
        bool enter_ = false;
        bool pressed_ = false;
        int normal_color_ = 0xffffff;
        int enter_color_ = 0xf0f0f0;
        int pressed_color_ = 0xd9d9d9;
        int radius_ = 0;
        int gr_from_color_ = 0;
        int gr_to_color_ = 0;
    };

}

#endif //GAMMARAYPC_CLICKABLE_WIDGET_H
