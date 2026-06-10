//
// Created by RGAA on 15/11/2025.
//

#ifndef GAMMARAYPREMIUM_IMAGE_GENERATOR_H
#define GAMMARAYPREMIUM_IMAGE_GENERATOR_H

#ifdef WIN32

#include <memory>
#include <string>

namespace tc
{
    class Image;

    class ImageGenerator {
    public:
        static std::shared_ptr<Image> CreateGrayscaleWithText(int w, int h, int bg_color, int font_color, int font_size, bool bold, const std::string& text);
    };

}

#endif

#endif //GAMMARAYPREMIUM_IMAGE_GENERATOR_H
