//
// Created by RGAA on 15/11/2025.
//

#include "image_generator.h"

#ifdef WIN32

#include <windows.h>
#include "px_common_new/image.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"

namespace tc
{

    std::shared_ptr<Image> ImageGenerator::CreateGrayscaleWithText(int w, int h, int bg_color, int font_color, int font_size, bool bold, const std::string& text) {
        LOGI("Start creating image: {}x{} bg:{} font:{}", w, h, bg_color, font_color);

        if (w <= 0 || h <= 0) {
            LOGE("Invalid dimensions");
            return nullptr;
        }

        if (bg_color < 0 || bg_color > 255 || font_color < 0 || font_color > 255) {
            LOGE("Invalid color values");
            return nullptr;
        }

        // Create 32bpp DIB for GDI drawing
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC hdcMem = CreateCompatibleDC(nullptr);
        if (!hdcMem) {
            LOGE("Failed to create compatible DC");
            return nullptr;
        }

        HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbm) {
            LOGE("Failed to create DIB section");
            DeleteDC(hdcMem);
            return nullptr;
        }

        HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, hbm);

        // Fill background
        RECT rc = {0, 0, w, h};
        HBRUSH hbr = CreateSolidBrush(RGB(bg_color, bg_color, bg_color));
        FillRect(hdcMem, &rc, hbr);
        DeleteObject(hbr);

        // Draw text
        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(font_color, font_color, font_color));

        HFONT hfont = CreateFontA(font_size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
        HFONT oldFont = (HFONT)SelectObject(hdcMem, hfont);

        auto wtext = StringUtil::ToWString(text);
        DrawTextW(hdcMem, wtext.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Convert 32bpp BGRA to grayscale Image
        auto image = Image::Make(nullptr, w, h, 1);
        auto img_data = image->GetData();
        uint8_t* dst = (uint8_t*)img_data->DataAddr();
        uint8_t* src = (uint8_t*)bits;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = (y * w + x) * 4;
                uint8_t b = src[idx];
                uint8_t g = src[idx + 1];
                uint8_t r = src[idx + 2];
                // Luminance formula
                dst[y * w + x] = (uint8_t)((r * 76 + g * 150 + b * 29) >> 8);
            }
        }

        SelectObject(hdcMem, oldFont);
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hfont);
        DeleteObject(hbm);
        DeleteDC(hdcMem);

        return image;
    }

}

#endif
