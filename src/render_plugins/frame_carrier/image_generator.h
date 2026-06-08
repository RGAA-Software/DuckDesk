//
// Created by RGAA on 15/11/2025.
//

#ifndef GAMMARAYPREMIUM_IMAGE_GENERATOR_H
#define GAMMARAYPREMIUM_IMAGE_GENERATOR_H

#ifdef WIN32

#include <QImage>
#include <QPainter>
#include <QColor>

namespace tc
{
    class ImageGenerator {
    public:
        static QImage CreateGrayscaleWithText(int w, int h, int bg_color, int font_color, int font_size, bool bold, const QString& text);


    };

}

#endif

#endif //GAMMARAYPREMIUM_IMAGE_GENERATOR_H
