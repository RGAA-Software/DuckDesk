//
// Created by RGAA on 1/04/2025.
//

#ifndef GAMMARAY_TC_TOOLTIP_H
#define GAMMARAY_TC_TOOLTIP_H

#include <QWidget>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QMainWindow>

namespace tc
{
    class TcToolTip : public QWidget {
    public:
        explicit TcToolTip(QWidget* parent = nullptr);
        void paintEvent(QPaintEvent *event) override;
        void SetText(const QString& text);

    private:
        QLabel* label_ = nullptr;
    };
}

#endif //GAMMARAY_TC_TOOLTIP_H
