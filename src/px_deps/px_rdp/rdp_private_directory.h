#pragma once

#ifdef _WIN32
#include <filesystem>
#include "px_common/win32/unique_win_handle.h"

namespace px::rdp {
// Requires an installer-controlled parent. Holds the directory against rename;
// rejects reparse points, permissive/changed ACLs and an unrelated owner.
[[nodiscard]] UniqueWinHandle OpenPrivateRdpDirectory(const std::filesystem::path& path, bool create = false);
} // namespace px::rdp
#endif
