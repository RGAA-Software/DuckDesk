#include "qwidget_helper.h"
#ifdef WIN32
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace tc {
   void QWidgetHelper::SetBorderInFullScreen(HWND hwnd, bool hasBorder) {
#ifdef WIN32
        if (!hwnd) return;
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        if (hasBorder) {
            style |= WS_BORDER;
            MARGINS margins = { 0, 0, 0, 1 };
            DwmExtendFrameIntoClientArea(hwnd, &margins);
        }
        else {
            style &= ~(WS_BORDER);
        }
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
#endif
    }
}
#endif // WIN32
