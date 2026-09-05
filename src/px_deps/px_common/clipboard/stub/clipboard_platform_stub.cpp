#include "px_common/clipboard/stub/clipboard_platform_stub.h"
#include "px_common/log.h"

namespace px::clipboard
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
