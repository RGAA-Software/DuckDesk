#pragma once

#include <string_view>

namespace px::ft {

struct FtTerminalInfo final {
    std::string_view status;
    std::string_view reason;
    bool success = false;
    bool resumable = false;
};

// Converts the engine's compatibility error string into stable audit/UI semantics.
// The original detail remains available to the caller and is never replaced by this code.
[[nodiscard]] FtTerminalInfo ClassifyTerminal(std::string_view error_or_empty);

} // namespace px::ft
