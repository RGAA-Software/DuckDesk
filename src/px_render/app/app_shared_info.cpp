#include "app_shared_info.h"
#include <Windows.h>
#include <sddl.h>
#include <format>
#include <limits>
#include <optional>
#include <vector>
#include "px_common/folder_util.h"
#include "px_common/log.h"

namespace px {
namespace {
struct LocalMemoryCloser final {
    void operator()(void* value) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) LocalAlloc output ownership.
        if (value) {
            LocalFree(value);
        }
    }
};
using LocalMemory = std::unique_ptr<void, LocalMemoryCloser>;
using LocalString = std::unique_ptr<wchar_t, LocalMemoryCloser>;

std::optional<std::wstring> UserSid(const UniqueWinHandle& token) {
    DWORD length{};
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &length);
    if (!length) {
        return {};
    }
    std::vector<std::byte> information(length);
    if (!GetTokenInformation(token.get(), TokenUser, information.data(), length, &length)) {
        return {};
    }
    LPWSTR result{}; // NOLINT(gammaray-raw-pointer-boundary) SID string API output immediately wrapped.
    if (!ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(information.data())->User.Sid, &result)) {
        return {};
    }
    const LocalString sid{result};
    return std::wstring(sid.get());
}
} // namespace

std::filesystem::path AppSharedInfo::BootConfigPath(uint32_t pid) {
    return std::filesystem::path(FolderUtil::GetProgramDataPath()) / L"hook_boot" / std::format(L"application_{}.bin", pid);
}

std::shared_ptr<AppSharedInfo> AppSharedInfo::Make(const std::shared_ptr<RdContext>& context) {
    return std::make_shared<AppSharedInfo>(context);
}

AppSharedInfo::AppSharedInfo(const std::shared_ptr<RdContext>& context) : context_(context) {}

bool AppSharedInfo::WriteBootConfig(const UniqueWinHandle& admitted_process, const std::string& data) {
    if (!admitted_process || data.size() > std::numeric_limits<DWORD>::max()) {
        return false;
    }
    HANDLE game_result{}; // NOLINT(gammaray-raw-pointer-boundary) Win32 token output immediately wrapped.
    if (!OpenProcessToken(admitted_process.get(), TOKEN_QUERY, &game_result)) {
        return false;
    }
    const UniqueWinHandle game_token{game_result};
    HANDLE writer_result{}; // NOLINT(gammaray-raw-pointer-boundary) Win32 token output immediately wrapped.
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &writer_result)) {
        return false;
    }
    const UniqueWinHandle writer_token{writer_result};
    const auto game_sid = UserSid(game_token);
    const auto writer_sid = UserSid(writer_token);
    if (!game_sid || !writer_sid) {
        return false;
    }
    const auto sddl = std::format(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;{})(A;;GR;;;{})", *writer_sid, *game_sid);
    PSECURITY_DESCRIPTOR descriptor_result{}; // NOLINT(gammaray-raw-pointer-boundary) SDDL output immediately wrapped.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor_result, nullptr)) {
        return false;
    }
    const LocalMemory descriptor{descriptor_result};
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = descriptor.get();
    const auto pid = GetProcessId(admitted_process.get());
    const auto path = BootConfigPath(pid);
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    // Protect before writing secrets, including when replacing a stale PID's file. Never follow a reparse point.
    const UniqueWinHandle file{CreateFileW(path.c_str(), GENERIC_WRITE | WRITE_DAC, FILE_SHARE_READ, &security, OPEN_ALWAYS,
                                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file || file.get() == INVALID_HANDLE_VALUE) {
        return false;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(file.get(), &information) || (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        information.nNumberOfLinks != 1 ||
        !SetKernelObjectSecurity(file.get(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor.get())) {
        return false;
    }
    DWORD written{};
    if (!SetEndOfFile(file.get()) || !WriteFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr) ||
        written != data.size()) {
        return false;
    }
    LOGI("Wrote protected hook bootstrap pid={} bytes={}", pid, written);
    return true;
}

void AppSharedInfo::Exit() {}
} // namespace px
