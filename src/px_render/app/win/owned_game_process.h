#pragma once

#include <filesystem>
#include <memory>
#include <string_view>
#include <atomic>
#include <mutex>
#include <vector>
#include <utility>
#include "px_common/win32/unique_win_handle.h"

namespace px {

enum class GameTokenPolicy { kInherit, kStandardUser };

// No API can adopt an existing process. The root is assigned while suspended;
// only its descendants can inherit this unnamed, non-inheritable Job.
class OwnedGameProcess final {
    struct ConstructionKey {};

  public:
    static std::shared_ptr<OwnedGameProcess> Launch(const std::filesystem::path& executable, std::wstring_view arguments, bool console_user = true);
    using EnvironmentOverrides = std::vector<std::pair<std::wstring, std::wstring>>;
    static std::shared_ptr<OwnedGameProcess> LaunchSuspended(const std::filesystem::path& executable, std::wstring_view arguments,
                                                             bool console_user = true, const EnvironmentOverrides& overrides = {},
                                                             GameTokenPolicy token_policy = GameTokenPolicy::kInherit);
    OwnedGameProcess(ConstructionKey, UniqueWinHandle job, UniqueWinHandle root, UniqueWinHandle thread);
    ~OwnedGameProcess();
    [[nodiscard]] DWORD RootPid() const;
    [[nodiscard]] bool HasLiveProcesses() const;
    [[nodiscard]] std::shared_ptr<UniqueWinHandle> Acquire(DWORD pid, const std::filesystem::path& expected, bool allow_descendant) const;
    void Stop();
    [[nodiscard]] bool Resume();

  private:
    UniqueWinHandle job_{};
    UniqueWinHandle root_{};
    UniqueWinHandle thread_{};
    std::mutex launch_mutex_{};
    std::atomic_bool stopped_{};
};

} // namespace px
