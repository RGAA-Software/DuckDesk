#ifndef TC_COMMON_NEW_CLIPBOARD_PLATFORM_WIN_H
#define TC_COMMON_NEW_CLIPBOARD_PLATFORM_WIN_H

#include "tc_common_new/clipboard/clipboard_platform.h"

namespace tc::clipboard
{

    class PlatformWin final : public IPlatform {
    public:
        bool Read(Content& out) override;
        bool WriteText(const std::string& utf8_text) override;
        bool Clear() override;
    };

}

#endif
