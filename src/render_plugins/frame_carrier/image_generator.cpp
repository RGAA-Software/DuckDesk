//
// Created by RGAA on 15/11/2025.
//

#include "image_generator.h"

#ifdef WIN32
#include <QDebug>

namespace tc
{

    QImage ImageGenerator::CreateGrayscaleWithText(int w, int h, int bg_color, int font_color, int font_size, bool bold, const QString &text) {
        qDebug() << "Start creating image:" << w << "x" << h << "bg:" << bg_color << "font:" << font_color;

        // 参数验证
        if (w <= 0 || h <= 0) {
            qDebug() << "Invalid dimensions";
            return QImage();
        }

        if (bg_color < 0 || bg_color > 255 || font_color < 0 || font_color > 255) {
            qDebug() << "Invalid color values";
            return QImage();
        }

        QImage image(w, h, QImage::Format_Grayscale8);
        qDebug() << "Image created, format:" << image.format() << "isNull:" << image.isNull();

        // 填充背景
        image.fill(static_cast<uchar>(bg_color));
        qDebug() << "Background filled";

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        qDebug() << "Painter created, isActive:" << painter.isActive();

        if (!painter.isActive()) {
            qDebug() << "Painter failed to initialize!";
            return image;
        }

        // 设置颜色
        painter.setPen(QColor(font_color, font_color, font_color));
        qDebug() << "Pen set";

        // 设置字体
        QFont font;
        font.setPointSize(font_size);
        font.setBold(bold);
        painter.setFont(font);

        painter.drawText(image.rect(), Qt::AlignCenter, text);

        painter.end();

        return image;
    }

}


#endif