#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <memory>

namespace px
{
    class Data;
    class DdaCaptureSource;
    class CaptureCursorBitmap;

    class CursorCapture {
    public:
        explicit CursorCapture(const std::shared_ptr<DdaCaptureSource>& owner);
        void Capture();

    private:
        static bool CaptureCursorIcon(
            CaptureCursorBitmap& data,
            HICON icon); // NOLINT(gammaray-raw-pointer-boundary): borrowed Win32 cursor handle

    private:
        std::weak_ptr<DdaCaptureSource> owner_;

        std::shared_ptr<Data> last_cursor_bitmap_data_ = nullptr;
        uint64_t last_timestamp_ = 0;
    };

}
