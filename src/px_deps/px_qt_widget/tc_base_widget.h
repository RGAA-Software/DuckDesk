//
// Created by RGAA on 26/04/2025.
//

#ifndef GAMMARAY_TC_BASE_WIDGET_H
#define GAMMARAY_TC_BASE_WIDGET_H

#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QBrush>
#include <functional>

namespace tc
{

    using OnEnterWidgetCallback = std::function<void(QWidget* w, bool enter)>;
    using OnClickWidgetCallback = std::function<void(QWidget* w)>;

    class TcBaseWidget : public QWidget {
    public:
        explicit TcBaseWidget(QWidget* parent = nullptr);

        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;

        void SetOnEnterWidgetCallback(OnEnterWidgetCallback&& cbk);
        void SetOnClickWidgetCallback(OnClickWidgetCallback&& cbk);

    protected:
        bool mouse_enter_ =  false;
        OnEnterWidgetCallback enter_cbk_;
        OnClickWidgetCallback click_cbk_;

    };

}

#endif //GAMMARAY_TC_BASE_WIDGET_H
