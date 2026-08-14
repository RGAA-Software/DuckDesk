#include "tc_common_new/clipboard/stub/clipboard_platform_stub.h"
#include "tc_common_new/log.h"

namespace tc::clipboard
{

    bool PlatformStub::Read(Content& out) {
        out = {};
        LOGE("clipboard platform is not implemented on this OS");
        return false;
    }

    bool PlatformStub::WriteText(const std::string& utf8_text) {
        LOGE("clipboard platform is not implemented on this OS");
        return false;
    }

    bool PlatformStub::Clear() {
        return false;
    }

}
