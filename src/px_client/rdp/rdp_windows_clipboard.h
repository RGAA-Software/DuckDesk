#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "rdp_clipboard_content.h"

namespace px::rdp {

class WindowsClipboard final {
public:
    WindowsClipboard() = default;
    [[nodiscard]] std::optional<ClipboardContent> ReadChanged();
    [[nodiscard]] bool Write(const ClipboardContent& content);

private:
    std::uint32_t sequence_{};
    std::shared_ptr<ClipboardStagingDirectory> staging_{};
};

}  // namespace px::rdp
