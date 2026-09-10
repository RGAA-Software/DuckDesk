#include "owned_game_process.h"
#include "game_process_identity.h"
#include <UserEnv.h>
#include <WtsApi32.h>
#include <vector>
#include <algorithm>
#include <array>

namespace px {
namespace {
struct EnvironmentCloser final {
    void operator()(void* value) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): UserEnv ABI; unique owner.
        if (value) {
            DestroyEnvironmentBlock(value);
        }
    }
};
using Environment = std::unique_ptr<void, EnvironmentCloser>;
struct CurrentEnvironmentCloser final {
    void operator()(wchar_t* value) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) Win32 environment allocation ABI.
        if (value) {
            FreeEnvironmentStringsW(value);
        }
    }
};
using CurrentEnvironment = std::unique_ptr<wchar_t, CurrentEnvironmentCloser>;
} // namespace

OwnedGameProcess::OwnedGameProcess(ConstructionKey, UniqueWinHandle job, UniqueWinHandle root, UniqueWinHandle thread)
    : job_(std::move(job)), root_(std::move(root)), thread_(std::move(thread)) {}

OwnedGameProcess::~OwnedGameProcess() {
    Stop();
}

std::shared_ptr<OwnedGameProcess> OwnedGameProcess::Launch(const std::filesystem::path& executable, std::wstring_view arguments, bool console_user) {
    const auto owner = LaunchSuspended(executable, arguments, console_user);
    return owner && owner->Resume() ? owner : std::shared_ptr<OwnedGameProcess>{};
}

std::shared_ptr<OwnedGameProcess> OwnedGameProcess::LaunchSuspended(const std::filesystem::path& executable, std::wstring_view arguments,
                                                                    bool console_user, const EnvironmentOverrides& overrides,
                                                                    GameTokenPolicy token_policy) {
    const auto path = NormalizeGameExecutable(executable);
    auto command = GameCommandLine(executable, arguments);
    if (!path || !command) {
        return {};
    }
    UniqueWinHandle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        return {};
    }
    UniqueWinHandle token{};
    Environment environment{};
    if (console_user) {
        const auto session = WTSGetActiveConsoleSessionId();
        HANDLE raw_token{}; // NOLINT(gammaray-raw-pointer-boundary): WTSQueryUserToken out parameter immediately wrapped.
        if (session != 0xFFFFFFFF && WTSQueryUserToken(session, &raw_token)) {
            token.reset(raw_token);
            void* raw_environment{}; // NOLINT(gammaray-raw-pointer-boundary): CreateEnvironmentBlock out parameter immediately wrapped.
            if (!CreateEnvironmentBlock(&raw_environment, token.get(), FALSE)) {
                return {};
            }
            environment.reset(raw_environment);
        } else {
            // No fallback to SYSTEM. A non-service caller may launch only as itself.
            DWORD current_session{};
            if (!ProcessIdToSessionId(GetCurrentProcessId(), &current_session) || current_session == 0 || current_session != session) {
                return {};
            }
        }
    }
    if (token_policy == GameTokenPolicy::kStandardUser) {
        const bool current_user_token = !token;
        if (!token) {
            HANDLE source_token{}; // NOLINT(gammaray-raw-pointer-boundary) Win32 token output immediately owned.
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT, &source_token)) {
                return {};
            }
            token.reset(source_token);
        }
        TOKEN_ELEVATION elevation{};
        DWORD returned{};
        if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &returned)) {
            return {};
        }
        if (elevation.TokenIsElevated) {
            HANDLE restricted_token{}; // NOLINT(gammaray-raw-pointer-boundary) Win32 restricted token output immediately owned.
            if (!CreateRestrictedToken(token.get(), LUA_TOKEN, 0, nullptr, 0, nullptr, 0, nullptr, &restricted_token)) {
                return {};
            }
            token.reset(restricted_token);
            // Administrator-only default object ACLs would prevent the restricted child from initializing its own DLLs/threads.
            DWORD user_size{};
            GetTokenInformation(token.get(), TokenUser, nullptr, 0, &user_size);
            std::vector<std::byte> user(user_size);
            if (!user_size || !GetTokenInformation(token.get(), TokenUser, user.data(), user_size, &user_size)) {
                return {};
            }
            TOKEN_OWNER owner{};
            owner.Owner = reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid;
            std::array<std::byte, SECURITY_MAX_SID_SIZE> system_sid{};
            DWORD system_size = static_cast<DWORD>(system_sid.size());
            std::array<std::byte, sizeof(ACL) + 2 * (sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE)> acl{};
            if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system_sid.data(), &system_size) ||
                !InitializeAcl(reinterpret_cast<PACL>(acl.data()), static_cast<DWORD>(acl.size()), ACL_REVISION) ||
                !AddAccessAllowedAce(reinterpret_cast<PACL>(acl.data()), ACL_REVISION, GENERIC_ALL, owner.Owner) ||
                !AddAccessAllowedAce(reinterpret_cast<PACL>(acl.data()), ACL_REVISION, GENERIC_ALL, system_sid.data())) {
                return {};
            }
            TOKEN_DEFAULT_DACL default_dacl{};
            default_dacl.DefaultDacl = reinterpret_cast<PACL>(acl.data());
            if (!SetTokenInformation(token.get(), TokenOwner, &owner, sizeof(owner)) ||
                !SetTokenInformation(token.get(), TokenDefaultDacl, &default_dacl, sizeof(default_dacl))) {
                return {};
            }
            // Loader checks integrity level, not just TokenElevation. LUA_TOKEN alone retains the original high integrity label.
            std::array<std::byte, SECURITY_MAX_SID_SIZE> medium_sid{};
            DWORD sid_size = static_cast<DWORD>(medium_sid.size());
            if (!CreateWellKnownSid(WinMediumLabelSid, nullptr, medium_sid.data(), &sid_size)) {
                return {};
            }
            TOKEN_MANDATORY_LABEL label{};
            label.Label.Sid = medium_sid.data();
            label.Label.Attributes = SE_GROUP_INTEGRITY;
            if (!SetTokenInformation(token.get(), TokenIntegrityLevel, &label, sizeof(label) + sid_size)) {
                return {};
            }
            if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &returned) || elevation.TokenIsElevated) {
                return {};
            }
        } else if (current_user_token) {
            // An ordinary desktop caller needs no CreateProcessAsUser privileges to launch as itself.
            token.reset();
        }
    }
    const CurrentEnvironment current_environment{environment ? nullptr : GetEnvironmentStringsW()};
    if (!environment && !current_environment) {
        return {};
    }
    std::vector<std::wstring> entries{};
    for (std::size_t offset{};;) {
        // Borrow only while reading the OS-owned double-NUL block; retain each entry by value.
        const std::wstring_view entry{environment ? static_cast<const wchar_t*>(environment.get()) + offset : current_environment.get() + offset};
        if (entry.empty()) {
            break;
        }
        entries.emplace_back(entry);
        offset += entry.size() + 1;
    }
    for (const auto& [name, value] : overrides) {
        if (name.empty() || name.find_first_of(L"=\0", 0, 2) != std::wstring::npos || value.find(L'\0') != std::wstring::npos) {
            return {};
        }
        const auto prefix = name + L"=";
        std::erase_if(entries, [&prefix](const std::wstring& entry) {
            return entry.size() >= prefix.size() && _wcsnicmp(entry.c_str(), prefix.c_str(), prefix.size()) == 0;
        });
        entries.push_back(prefix + value);
    }
    std::sort(entries.begin(), entries.end(),
              [](const std::wstring& left, const std::wstring& right) { return _wcsicmp(left.c_str(), right.c_str()) < 0; });
    std::vector<wchar_t> child_environment{};
    for (const auto& entry : entries) {
        child_environment.insert(child_environment.end(), entry.begin(), entry.end());
        child_environment.push_back(L'\0');
    }
    child_environment.push_back(L'\0');
    if (entries.empty()) {
        child_environment.push_back(L'\0');
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    wchar_t desktop[]{L"WinSta0\\Default"};
    startup.lpDesktop = desktop;
    PROCESS_INFORMATION information{};
    const auto directory = path->parent_path().native();
    constexpr DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS;
    const auto started = token ? CreateProcessAsUserW(token.get(), path->c_str(), command->data(), nullptr, nullptr, FALSE, flags,
                                                      child_environment.data(), directory.c_str(), &startup, &information)
                               : CreateProcessW(path->c_str(), command->data(), nullptr, nullptr, FALSE, flags, child_environment.data(),
                                                directory.c_str(), &startup, &information);
    if (!started) {
        return {};
    }
    UniqueWinHandle root{information.hProcess};
    UniqueWinHandle thread{information.hThread};
    // No game code or launcher child runs before assignment succeeds.
    if (!AssignProcessToJobObject(job.get(), root.get())) {
        TerminateProcess(root.get(), 1);
        WaitForSingleObject(root.get(), 3000);
        return {};
    }
    return std::make_shared<OwnedGameProcess>(ConstructionKey{}, std::move(job), std::move(root), std::move(thread));
}

bool OwnedGameProcess::Resume() {
    std::scoped_lock lock(launch_mutex_);
    if (stopped_ || !thread_) {
        return false;
    }
    if (ResumeThread(thread_.get()) == static_cast<DWORD>(-1)) {
        return false;
    }
    thread_.reset();
    return true;
}

DWORD OwnedGameProcess::RootPid() const {
    return root_ ? GetProcessId(root_.get()) : 0;
}

bool OwnedGameProcess::HasLiveProcesses() const {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    return !stopped_ && job_ &&
           QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) &&
           accounting.ActiveProcesses > 0;
}

std::shared_ptr<UniqueWinHandle> OwnedGameProcess::Acquire(DWORD pid, const std::filesystem::path& expected, bool allow_descendant) const {
    if (stopped_ || !job_ || !pid || (!allow_descendant && pid != RootPid())) {
        return {};
    }
    UniqueWinHandle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid)};
    BOOL member{};
    if (!process || WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT || !IsProcessInJob(process.get(), job_.get(), &member) || !member) {
        return {};
    }
    std::vector<wchar_t> path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length) || !SameGameExecutable(expected, std::wstring(path.data(), length))) {
        return {};
    }
    if (stopped_) {
        return {};
    }
    return std::make_shared<UniqueWinHandle>(std::move(process));
}

void OwnedGameProcess::Stop() {
    std::scoped_lock lock(launch_mutex_);
    if (!stopped_.exchange(true) && job_) {
        TerminateJobObject(job_.get(), 0);
    }
}

} // namespace px
