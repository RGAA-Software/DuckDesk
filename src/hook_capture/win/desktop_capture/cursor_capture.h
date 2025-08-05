#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <memory>

namespace tc
{
    class MessageNotifier;
    class CaptureCursorBitmap;

    class CursorCapture {
    public:
        explicit CursorCapture(const std::shared_ptr<MessageNotifier>& msg_notifier);
        void Capture();

    private:
        static bool CaptureCursorIcon(CaptureCursorBitmap* data, HICON icon);

    private:
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;

    };

}
