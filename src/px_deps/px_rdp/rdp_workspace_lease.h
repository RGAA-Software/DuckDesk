#pragma once

#ifdef _WIN32
#include <filesystem>
#include <memory>
#include <string_view>

#include "px_common/win32/unique_win_handle.h"

namespace px::rdp {

// The Render owns this kernel lease before starting the proxy. Service restarts
// cannot release it; process death releases it automatically. The empty file is
// persistent, not a stale PID/lock marker to be removed during recovery.
class RdpWorkspaceLease final {
  public:
    [[nodiscard]] static std::unique_ptr<RdpWorkspaceLease> Acquire(const std::filesystem::path& private_root, std::string_view workspace_id);
    RdpWorkspaceLease(UniqueWinHandle directory, UniqueWinHandle handle) noexcept;
    RdpWorkspaceLease(const RdpWorkspaceLease&) = delete;
    RdpWorkspaceLease& operator=(const RdpWorkspaceLease&) = delete;

  private:
    UniqueWinHandle directory_{};
    UniqueWinHandle handle_{};
};

} // namespace px::rdp
#endif
