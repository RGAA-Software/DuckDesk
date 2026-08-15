#pragma once
#ifdef WIN32
#include <windows.h>

namespace tc  { 
    class QWidgetHelper {
    public:
        static void SetBorderInFullScreen(HWND hwnd, bool has_border);
    };
}

#endif //
