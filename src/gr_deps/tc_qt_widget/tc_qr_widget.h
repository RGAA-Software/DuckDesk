//
// Created by RGAA on 20/03/2025.
//

#ifndef GAMMARAY_TC_QR_WIDGET_H
#define GAMMARAY_TC_QR_WIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QPainter>

namespace tc
{

    class TcQRWidget : public QWidget {
    public:
        explicit TcQRWidget(QWidget* parent = nullptr);
        void SetQRPixmap(const QPixmap& pixmap);

        void paintEvent(QPaintEvent *event) override;

    private:
        QPixmap pixmap_;
    };

}

#endif //GAMMARAY_TC_QR_WIDGET_H
