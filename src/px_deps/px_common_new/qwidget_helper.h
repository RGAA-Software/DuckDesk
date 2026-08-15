#pragma once
#ifdef WIN32
#include <windows.h>

namespace px  { 
    class QWidgetHelper {
    public:
        static void SetBorderInFullScreen(HWND hwnd, bool has_border);
    };
}

#endif //
