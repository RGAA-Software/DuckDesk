#include "clipboard_platform.h"
#include "tc_common_new/time_util.h"

#if defined(WIN32)
#include "tc_common_new/clipboard/win/clipboard_platform_win.h"
#else
#include "tc_common_new/clipboard/stub/clipboard_platform_stub.h"
#endif

namespace tc::clipboard
{

    std::unique_ptr<IPlatform> CreatePlatform() {
#if defined(WIN32)
        return std::make_unique<PlatformWin>();
#else
        return std::make_unique<PlatformStub>();
#endif
    }

    bool WriteTextWithRetry(IPlatform& platform, const std::string& utf8_text, int max_attempts, int sleep_ms) {
        if (utf8_text.empty()) {
            return false;
        }
        Content current;
        for (int i = 0; i < max_attempts; ++i) {
            if (platform.Read(current) && current.text_ == utf8_text) {
                return true;
            }
            if (platform.WriteText(utf8_text)) {
                if (platform.Read(current) && current.text_ == utf8_text) {
                    return true;
                }
            }
            TimeUtil::DelayBySleep(sleep_ms);
        }
        return false;
    }

}
