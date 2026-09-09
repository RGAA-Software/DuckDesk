#include "owned_game_process.h"
#include "game_process_identity.h"
#include <UserEnv.h>
#include <WtsApi32.h>
#include <vector>

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
} // namespace

OwnedGameProcess::OwnedGameProcess(ConstructionKey, UniqueWinHandle job, UniqueWinHandle root) : job_(std::move(job)), root_(std::move(root)) {}

OwnedGameProcess::~OwnedGameProcess() {
    Stop();
}

std::shared_ptr<OwnedGameProcess> OwnedGameProcess::Launch(const std::filesystem::path& executable, std::wstring_view arguments, bool console_user) {
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
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    wchar_t desktop[]{L"WinSta0\\Default"};
    startup.lpDesktop = desktop;
    PROCESS_INFORMATION information{};
    const auto directory = path->parent_path().native();
    constexpr DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS;
    const auto started =
        token ? CreateProcessAsUserW(token.get(), path->c_str(), command->data(), nullptr, nullptr, FALSE, flags, environment.get(),
                                     directory.c_str(), &startup, &information)
              : CreateProcessW(path->c_str(), command->data(), nullptr, nullptr, FALSE, flags, nullptr, directory.c_str(), &startup, &information);
    if (!started) {
        return {};
    }
    UniqueWinHandle root{information.hProcess};
    UniqueWinHandle thread{information.hThread};
    // No game code or launcher child runs before assignment succeeds.
    if (!AssignProcessToJobObject(job.get(), root.get()) || ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        TerminateProcess(root.get(), 1);
        WaitForSingleObject(root.get(), 3000);
        return {};
    }
    return std::make_shared<OwnedGameProcess>(ConstructionKey{}, std::move(job), std::move(root));
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
    if (!stopped_.exchange(true) && job_) {
        TerminateJobObject(job_.get(), 0);
    }
}

} // namespace px
