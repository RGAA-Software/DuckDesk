//
// Created by RGAA on 23/03/2025.
//

#ifndef GAMMARAY_TC_CUSTOM_TITLEBAR_H
#define GAMMARAY_TC_CUSTOM_TITLEBAR_H

#include <QWidget>
#include <QPainter>
#include <QPaintEvent>

namespace tc
{

    using ClickCallback = std::function<void()>;

    class OperationIcon : public QWidget {
    public:

        OperationIcon(int radius, int normal_color, int enter_color, QWidget *parent = nullptr);
        void paintEvent(QPaintEvent *event) override;
        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;
        void SetOnClickCallback(ClickCallback &&cbk);

    private:
        int radius_ = 10;
        int normal_color_ = 0xff0099;
        int enter_color_ = 0xff000000;

        bool enter_ = false;
        bool pressed_ = false;

        ClickCallback click_cbk_;

    };

    class TcCustomTitleBar : public QWidget {
    public:
        TcCustomTitleBar(const QString& title, QWidget *parent = nullptr);
        ~TcCustomTitleBar();
        void paintEvent(QPaintEvent *event) override;
        void SetMinClickCallback(ClickCallback &&cbk);
        void SetCloseClickCallback(ClickCallback &&cbk);
        void mousePressEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;

    private:

        OperationIcon *min_btn_ = nullptr;
        OperationIcon *close_btn_ = nullptr;

        QPoint click_point_{};
        QPoint click_window_pos_{};
        bool left_pressed_ = false;

    };

}

#endif //GAMMARAY_TC_CUSTOM_TITLEBAR_H
