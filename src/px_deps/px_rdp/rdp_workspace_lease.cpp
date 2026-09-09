#include "rdp_workspace_lease.h"
#include "rdp_private_directory.h"

#ifdef _WIN32
#include <algorithm>
#include <utility>

namespace px::rdp {

RdpWorkspaceLease::RdpWorkspaceLease(UniqueWinHandle directory, UniqueWinHandle handle) noexcept
    : directory_(std::move(directory)), handle_(std::move(handle)) {}

std::unique_ptr<RdpWorkspaceLease> RdpWorkspaceLease::Acquire(const std::filesystem::path& private_root, std::string_view workspace_id) {
    const bool valid_id = !workspace_id.empty() && workspace_id.size() <= 128 && std::ranges::all_of(workspace_id, [](unsigned char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '-' || value == '_';
    });
    if (!valid_id || !private_root.is_absolute()) {
        return {};
    }
    auto directory = OpenPrivateRdpDirectory(private_root);
    if (!directory) {
        return {};
    }
    const auto path = private_root / (std::string(workspace_id) + ".runtime.lock");
    // Win32 handle ownership is wrapped at the acquisition boundary. No sharing:
    // another Render (including one from a restarted Service) must fail closed.
    auto handle = UniqueWinHandle{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!handle || handle.get() == INVALID_HANDLE_VALUE) {
        return {};
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle.get(), &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || info.nNumberOfLinks != 1) {
        return {};
    }
    return std::make_unique<RdpWorkspaceLease>(std::move(directory), std::move(handle));
}

} // namespace px::rdp
#endif
