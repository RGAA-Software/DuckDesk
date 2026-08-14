//
// Platform clipboard abstraction. Text/file-path I/O uses native OS APIs.
// Virtual file clipboard (OLE IDataObject on Windows) remains app-specific.
//

#ifndef TC_COMMON_NEW_CLIPBOARD_PLATFORM_H
#define TC_COMMON_NEW_CLIPBOARD_PLATFORM_H

#include "clipboard_types.h"
#include <functional>
#include <memory>

namespace tc::clipboard
{

    class IPlatform {
    public:
        virtual ~IPlatform() = default;

        // Read current clipboard text and/or local file paths.
        virtual bool Read(Content& out) = 0;

        // Write UTF-8 plain text. Returns false if clipboard is busy.
        virtual bool WriteText(const std::string& utf8_text) = 0;

        // Release clipboard ownership (e.g. OleSetClipboard(nullptr) on Windows).
        virtual bool Clear() = 0;
    };

    std::unique_ptr<IPlatform> CreatePlatform();

    // Write text with bounded retries (handles transient clipboard busy).
    bool WriteTextWithRetry(IPlatform& platform, const std::string& utf8_text, int max_attempts = 20, int sleep_ms = 5);

}

#endif
