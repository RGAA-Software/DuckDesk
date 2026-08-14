//
// Created by RGAA on 20/03/2025.
//

#include "tc_qr_widget.h"

namespace tc
{

    TcQRWidget::TcQRWidget(QWidget* parent) {

    }

    void TcQRWidget::SetQRPixmap(const QPixmap& pixmap) {
        pixmap_ = pixmap;
        repaint();
    }

    void TcQRWidget::paintEvent(QPaintEvent *event) {
        if (pixmap_.isNull()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        int size = pixmap_.width();
        const auto& image = pixmap_.toImage();

        int scale = 3;
        int image_height = scale * size;

        // left top
        {
            QPen pen(0x111111);
            pen.setWidth(2);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            int width = 7*scale;
            painter.drawRoundedRect(1, 1, width-2, width-2, width/2, width/2);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush(0x2979ff));
            int offset = 6;
            painter.drawRoundedRect(offset, offset, width-offset*2, width-offset*2, width-offset*2/2, width-offset*2/2);
        }

        // left bottom
        {
            QPen pen(0x111111);
            pen.setWidth(2);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            int width = 7*scale;
            int y_offset = image_height - width + 1;
            painter.drawRoundedRect(1, y_offset, width-2, width-2, width/2, width/2);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush(0x2979ff));
            int offset = 6;
            painter.drawRoundedRect(offset, y_offset + offset-1, width-offset*2, width-offset*2, width-offset*2/2, width-offset*2/2);
        }

        // right top
        {
            QPen pen(0x111111);
            pen.setWidth(2);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            int width = 7*scale;
            int x_offset = image_height - width + 1;
            painter.drawRoundedRect(x_offset, 1, width-2, width-2, width/2, width/2);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush(0x2979ff));
            int offset = 6;
            painter.drawRoundedRect(x_offset + offset-1, offset, width-offset*2, width-offset*2, width-offset*2/2, width-offset*2/2);
        }

        painter.setPen(Qt::NoPen);
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (x >= 0 && x <= 7 && y >= 0 && y <= 7) {
                    continue;
                }
                if (x >= 0 && x <= 7 && y >= image.height() - 7 && y <= image.height()) {
                    continue;
                }
                if (x >= image.width()-7 && x <= image.width() && y >= 0 && y <= 7) {
                    continue;
                }

                int scaled_icon_size = 6; //==> 18 / 3
                int offset = (pixmap_.width() - scaled_icon_size)/2;
                if (x >= offset-1 && x < offset+scaled_icon_size+1 && y >= offset-1 && y < offset+scaled_icon_size+1) {
                    continue;
                }

                QRgb rgb = image.pixel(x, y);
                if (qRed(rgb) == 0 && qGreen(rgb) == 0 && qBlue(rgb) == 0) {
                    painter.setBrush(QBrush(0x111111));
                }
                else {
                    painter.setBrush(QBrush(0xffffff));
                }
                //painter.drawPoint(x, y);
                painter.drawRoundedRect(3*x, 3*y, 3, 3, 1.5, 1.5);
            }
        }
    }

}