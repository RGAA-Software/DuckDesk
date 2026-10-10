#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "px_common/clipboard/clipboard_platform.h"

namespace px {

// Per-Render text synchronization for applications running in the existing user
// session. Platform calls close the clipboard before returning; no OS lock is
// retained between messages or during video/network work.
class ApplicationClipboard final {
public:
    explicit ApplicationClipboard(std::unique_ptr<clipboard::IPlatform> platform);
    bool WriteRemoteText(const std::string& text);
    [[nodiscard]] std::optional<std::string> ReadChangedText();
    void Refresh();

private:
    std::mutex mutex_{};
    std::unique_ptr<clipboard::IPlatform> platform_{};
    std::optional<std::string> observed_text_{};
};

}  // namespace px
