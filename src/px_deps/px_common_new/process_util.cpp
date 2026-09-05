//
// Created by RGAA  on 2024/2/13.
//

#include "process_util.h"
#ifdef WIN32
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"
#include "px_common_new/win32/unique_win_handle.h"
#include <UserEnv.h>
#include <TlHelp32.h>
#include <wtsapi32.h>
#include <ShlObj_core.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <cstddef>
#include <sstream>
#include <filesystem>
#include <memory>

namespace px
{

    namespace {
        struct ModuleCloser final {
            void operator()(std::remove_pointer_t<HMODULE>* module) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): HMODULE ABI.
                if (module != nullptr) {
                    FreeLibrary(module);
                }
            }
        };

        using UniqueModule = std::unique_ptr<std::remove_pointer_t<HMODULE>, ModuleCloser>;

        struct EnvironmentBlockCloser final {
            void operator()(void* environment) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): UserEnv ABI.
                if (environment != nullptr) {
                    DestroyEnvironmentBlock(environment);
                }
            }
        };

        using UniqueEnvironmentBlock = std::unique_ptr<void, EnvironmentBlockCloser>;

        std::wstring EscapeArg(const std::wstring& arg) {
            if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
                return arg;
            }
            std::wstring escaped = L"\"";
            for (size_t i = 0; i < arg.size(); ++i) {
                size_t backslashCount = 0;
                while (i < arg.size() && arg[i] == L'\\') {
                    backslashCount++;
                    i++;
                }
                if (i == arg.size()) {
                    escaped.append(backslashCount * 2, L'\\');
                    break;
                } else if (arg[i] == L'"') {
                    escaped.append(backslashCount * 2 + 1, L'\\');
                    escaped += L'"';
                } else {
                    escaped.append(backslashCount, L'\\');
                    escaped += arg[i];
                }
            }
            escaped += L'"';
            return escaped;
        }

        std::wstring BuildCommandLine(const std::string& exe_path, const std::vector<std::string>& args) {
            std::wstring cmdline;
            auto wexe = StringUtil::ToWString(exe_path);
            cmdline += EscapeArg(wexe);
            for (const auto& arg : args) {
                cmdline += L' ';
                cmdline += EscapeArg(StringUtil::ToWString(arg));
            }
            return cmdline;
        }
    }

    bool SetDpiAwarenessContext(DPI_AWARENESS_CONTEXT context) {
        using SetProcessDpiAwarenessFunc = BOOL(__stdcall*)(DPI_AWARENESS_CONTEXT);  // NOLINT(gammaray-raw-pointer-boundary): Win32 ABI.
        const UniqueModule user32{LoadLibraryW(L"User32.dll")};
        if (!user32) {
            return false;
        }
        const auto set_process_dpi_awareness = reinterpret_cast<SetProcessDpiAwarenessFunc>(
            GetProcAddress(user32.get(), "SetProcessDpiAwarenessContext"));
        return set_process_dpi_awareness != nullptr && set_process_dpi_awareness(context) != FALSE;
    }

    bool ProcessUtil::StartProcessAndWait(const std::string& exe_path, const std::vector<std::string>& args) {
        auto cmdline = BuildCommandLine(exe_path, args);
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            LOGE("StartProcessAndWait failed: {}, err: {}", exe_path, GetLastError());
            return false;
        }
        const UniqueWinHandle process{pi.hProcess};
        const UniqueWinHandle thread{pi.hThread};
        WaitForSingleObject(process.get(), INFINITE);
        DWORD exitCode{};
        GetExitCodeProcess(process.get(), &exitCode);
        if (exitCode != 0) {
            LOGE("Start process: {}, exit code: {}", exe_path, exitCode);
            return false;
        }
        return true;
    }

    uint32_t ProcessUtil::StartProcess(const std::string& exe_path, const std::vector<std::string>& args, bool detach, bool wait) {
        auto cmdline = BuildCommandLine(exe_path, args);
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        DWORD creationFlags = CREATE_NO_WINDOW;
        if (detach) {
            creationFlags = CREATE_NEW_CONSOLE;
        }
        if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, creationFlags, nullptr, nullptr, &si, &pi)) {
            LOGE("StartProcess failed: {}, err: {}", exe_path, GetLastError());
            return 0;
        }
        const UniqueWinHandle process{pi.hProcess};
        const UniqueWinHandle thread{pi.hThread};
        if (wait) {
            WaitForSingleObject(process.get(), INFINITE);
            return 0;
        }
        return pi.dwProcessId;
    }

    std::vector<std::string> ProcessUtil::StartProcessAndOutput(const std::string& exe_path, const std::vector<std::string>& args) {
        std::vector<std::string> output;

        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE read_pipe_raw{};  // NOLINT(gammaray-raw-pointer-boundary): CreatePipe out parameter, immediately RAII-wrapped.
        HANDLE write_pipe_raw{};  // NOLINT(gammaray-raw-pointer-boundary): CreatePipe out parameter, immediately RAII-wrapped.
        if (!CreatePipe(&read_pipe_raw, &write_pipe_raw, &sa, 0)) {
            LOGE("CreatePipe failed: {}", GetLastError());
            return output;
        }
        UniqueWinHandle read_pipe{read_pipe_raw};
        UniqueWinHandle write_pipe{write_pipe_raw};
        if (!SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0)) {
            LOGE("SetHandleInformation failed: {}", GetLastError());
            return output;
        }

        auto cmdline = BuildCommandLine(exe_path, args);
        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = write_pipe.get();
        si.hStdError = write_pipe.get();
        PROCESS_INFORMATION pi = {};

        if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            LOGE("StartProcessAndOutput failed: {}, err: {}", exe_path, GetLastError());
            return output;
        }
        const UniqueWinHandle process{pi.hProcess};
        const UniqueWinHandle thread{pi.hThread};

        write_pipe.reset();

        char buffer[4096]{};
        DWORD bytesRead{};
        std::string output_str;
        while (true) {
            const BOOL success = ReadFile(read_pipe.get(), buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
            if (!success || bytesRead == 0) {
                break;
            }
            output_str.append(buffer, bytesRead);
        }

        std::istringstream iss(output_str);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                output.push_back(line);
            }
        }

        WaitForSingleObject(process.get(), INFINITE);
        return output;
    }

    bool ProcessUtil::KillProcess(unsigned long pid) {
        const UniqueWinHandle process{OpenProcess(PROCESS_TERMINATE, FALSE, pid)};
        if (!process) {
            std::cerr << "OpenProcess failed: " << GetLastError() << std::endl;
            return false;
        }
        const BOOL result = TerminateProcess(process.get(), 1);
        return result != 0;
    }

    int ProcessUtil::GetPidByExeName(const std::string& exe_name) {
        int find_pid = 0;
        PROCESSENTRY32W pe32{};
        pe32.dwSize = sizeof(pe32);
        const UniqueWinHandle process_snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
        if (process_snapshot.get() == INVALID_HANDLE_VALUE) {
            return 0;
        }
        int cur_ses_id = GetCurrentSessionId();
        if(cur_ses_id == -1) {
            return 0;
        }
        std::wstring wname = StringUtil::ToWString(exe_name);
        BOOL bResult = Process32FirstW(process_snapshot.get(), &pe32);
        while (bResult) {
            bResult = Process32NextW(process_snapshot.get(), &pe32);
            if (_wcsicmp(pe32.szExeFile, wname.c_str()) == 0) {
                int ses_id = GetProcessSessionId(pe32.th32ProcessID);
                if(ses_id == cur_ses_id) {
                    find_pid = pe32.th32ProcessID;
                    break;
                }
            }
        }
        return find_pid;
    }


    static UniqueWinHandle DuplicateAdminToken() {
        const int explorer_pid = ProcessUtil::GetPidByExeName(kExploreName);
        if (explorer_pid == 0) {
            return {};
        }
        const UniqueWinHandle process{::OpenProcess(PROCESS_ALL_ACCESS, FALSE, explorer_pid)};
        if (!process) {
            return {};
        }
        HANDLE token_raw{};  // NOLINT(gammaray-raw-pointer-boundary): OpenProcessToken out parameter, immediately RAII-wrapped.
        if (!OpenProcessToken(process.get(), TOKEN_ALL_ACCESS, &token_raw)) {
            return {};
        }
        const UniqueWinHandle token{token_raw};
        HANDLE duplicate_raw{};  // NOLINT(gammaray-raw-pointer-boundary): DuplicateTokenEx out parameter, immediately RAII-wrapped.
        if (!DuplicateTokenEx(token.get(), MAXIMUM_ALLOWED, nullptr, SecurityIdentification, TokenPrimary, &duplicate_raw)) {
            return {};
        }
        return UniqueWinHandle{duplicate_raw};
    }

    bool ProcessUtil::StartProcessInWorkDir(const std::string &work_dir, const std::string &cmdline,
                                            const std::vector<std::string> &args) {
        STARTUPINFOW si{};
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi{};

        auto w_cmdline = StringUtil::ToWString(cmdline);
        auto w_work_dir = StringUtil::ToWString(work_dir);

        if (IsUserAnAdmin()) {
            const auto user_token = DuplicateAdminToken();
            if (!user_token) {
                LOGE("DuplicateLimitPrivilegeToken failed.");
                return false;
            }
            LOGI("IsUserAnAdmin，create process with token.");

            constexpr DWORD create_flag = CREATE_NO_WINDOW | ABOVE_NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT;
            void* environment_raw{};  // NOLINT(gammaray-raw-pointer-boundary): CreateEnvironmentBlock out parameter, immediately RAII-wrapped.
            if (!CreateEnvironmentBlock(&environment_raw, user_token.get(), TRUE)) {
                LOGE("CreateEnvironmentBlock failed: {}", GetLastError());
                return false;
            }
            const UniqueEnvironmentBlock environment{environment_raw};
            if (!CreateProcessWithTokenW(user_token.get(), 0, nullptr, w_cmdline.data(), create_flag, environment.get(),
                                         w_work_dir.c_str(), &si, &pi)) {
                LOGE("CreateProcessWithTokenW failed: {}", GetLastError());
                return false;
            }
        }
        else {
            if (!CreateProcessW(
                    NULL,
                    w_cmdline.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    w_work_dir.c_str(),
                    &si,
                    &pi
            )) {
                LOGE("CreateProcessW failed: {}", GetLastError());
                return false;
            }
        }

        LOGI("==> CreateProcessSuccess...{} {}", pi.dwProcessId, pi.dwThreadId);

        std::cout << "pid:" << pi.dwProcessId << " tid:" << pi.dwThreadId << std::endl;

        const UniqueWinHandle process{pi.hProcess};
        const UniqueWinHandle thread{pi.hThread};
        WaitForSingleObject(process.get(), INFINITE);
        LOGI("==> process exit....");
        return true;
    }

    bool ProcessUtil::StartProcessInSameUser(const std::wstring& cmdline, const std::wstring& work_dir, bool wait) {
        HANDLE token_raw{};  // NOLINT(gammaray-raw-pointer-boundary): OpenProcessToken out parameter, immediately RAII-wrapped.
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token_raw)) {
            LOGE("StartProcessInSameUser OpenProcessToken failed.");
            return false;
        }
        const UniqueWinHandle token{token_raw};

        HANDLE duplicate_raw{};  // NOLINT(gammaray-raw-pointer-boundary): DuplicateTokenEx out parameter, immediately RAII-wrapped.
        if (!DuplicateTokenEx(token.get(), TOKEN_ALL_ACCESS, nullptr, SecurityIdentification, TokenPrimary, &duplicate_raw)) {
            LOGE("DuplicateTokenEx failed.");
            return false;
        }
        const UniqueWinHandle duplicate_token{duplicate_raw};

        DWORD session_id = WTSGetActiveConsoleSessionId();

        if (session_id == 0xFFFFFFFF) {
            return false;
        }

        if (!SetTokenInformation(duplicate_token.get(), TokenSessionId, &session_id, sizeof(session_id))) {
            LOGE("SetTokenInformation failed: {}", GetLastError());
            return false;
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        wchar_t desktop[] = L"WinSta0\\Default";
        si.lpDesktop = desktop;
        si.wShowWindow = SW_SHOW;
        si.dwFlags = STARTF_USESHOWWINDOW;

        void* environment_raw{};  // NOLINT(gammaray-raw-pointer-boundary): CreateEnvironmentBlock out parameter, immediately RAII-wrapped.
        if (!CreateEnvironmentBlock(&environment_raw, duplicate_token.get(), FALSE)) {
            LOGE("CreateEnvironmentBlock failed: {}", GetLastError());
            return false;
        }
        const UniqueEnvironmentBlock environment{environment_raw};
        PROCESS_INFORMATION process_info{};
        constexpr DWORD creation_flags = NORMAL_PRIORITY_CLASS | CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;
        auto mutable_cmdline = cmdline;

        LOGI("workdir: {}", StringUtil::ToUTF8(work_dir));
        LOGI("cmdline: {}", StringUtil::ToUTF8(cmdline));

        if (!CreateProcessAsUserW(duplicate_token.get(), nullptr, mutable_cmdline.data(), nullptr, nullptr, FALSE, creation_flags,
                                  environment.get(), work_dir.c_str(), &si, &process_info)) {
            LOGE("CreateProcessAsUserW failed. error code: {}", GetLastError());
            return false;
        }
        const UniqueWinHandle process{process_info.hProcess};
        const UniqueWinHandle thread{process_info.hThread};
        if (wait) {
            const auto wait_result = ::WaitForSingleObject(process.get(), INFINITE);
            LOGI("wait result: {:x}, last error: {:x}", wait_result, GetLastError());
        }

        return true;
    }

    uint32_t ProcessUtil::StartProcessAsCurrentUser(const std::string& exe_path, const std::vector<std::string>& args) {
        // 以控制台会话的登录用户身份启动（服务/SYSTEM 上下文下，游戏等交互程序
        // 若直接 CreateProcess 会落在 SYSTEM profile，网络代理/用户配置都不对）。
        // 返回 pid；拿不到控制台用户 token 时返回 0，调用方自行回退。
        DWORD session_id = WTSGetActiveConsoleSessionId();
        if (session_id == 0xFFFFFFFF) {
            LOGE("StartProcessAsCurrentUser, WTSGetActiveConsoleSessionId failed");
            return 0;
        }
        HANDLE user_token_raw{};  // NOLINT(gammaray-raw-pointer-boundary): WTSQueryUserToken out parameter, immediately RAII-wrapped.
        if (!WTSQueryUserToken(session_id, &user_token_raw)) {
            LOGE("StartProcessAsCurrentUser, WTSQueryUserToken failed: {}", GetLastError());
            return 0;
        }
        const UniqueWinHandle user_token{user_token_raw};
        HANDLE token_duplicate_raw{};  // NOLINT(gammaray-raw-pointer-boundary): DuplicateTokenEx out parameter, immediately RAII-wrapped.
        if (!DuplicateTokenEx(user_token.get(), TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &token_duplicate_raw)) {
            LOGE("StartProcessAsCurrentUser, DuplicateTokenEx failed: {}", GetLastError());
            return 0;
        }
        const UniqueWinHandle duplicate_token{token_duplicate_raw};
        void* environment_raw{};  // NOLINT(gammaray-raw-pointer-boundary): CreateEnvironmentBlock out parameter, immediately RAII-wrapped.
        if (!CreateEnvironmentBlock(&environment_raw, duplicate_token.get(), FALSE)) {
            LOGE("StartProcessAsCurrentUser, CreateEnvironmentBlock failed: {}", GetLastError());
            return 0;
        }
        const UniqueEnvironmentBlock environment{environment_raw};

        auto cmdline = BuildCommandLine(exe_path, args);
        std::wstring work_dir = std::filesystem::path(StringUtil::ToWString(exe_path)).parent_path().wstring();
        STARTUPINFOW si = { sizeof(si) };
        wchar_t desktop[] = L"WinSta0\\Default";
        si.lpDesktop = desktop;
        si.wShowWindow = SW_SHOW;
        si.dwFlags = STARTF_USESHOWWINDOW;
        PROCESS_INFORMATION pi = {};
        // 不加 CREATE_NEW_CONSOLE：GUI 游戏不需要在用户桌面闪控制台窗口
        DWORD flags = CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS;
        const BOOL ok = CreateProcessAsUserW(duplicate_token.get(), nullptr, cmdline.data(), nullptr, nullptr, FALSE, flags,
                                             environment.get(), work_dir.c_str(), &si, &pi);
        const DWORD pid = ok ? pi.dwProcessId : 0;
        if (!ok) {
            LOGE("StartProcessAsCurrentUser, CreateProcessAsUser failed: {}, err: {}", exe_path, GetLastError());
        } else {
            LOGI("StartProcessAsCurrentUser: {} pid={} (session {})", exe_path, pid, session_id);
            const UniqueWinHandle process{pi.hProcess};
            const UniqueWinHandle thread{pi.hThread};
        }
        return pid;
    }

    bool ProcessUtil::StartProcessInCurrentUser(const std::wstring& cmdline, const std::wstring& work_dir, bool wait) {
        const DWORD session_id = WTSGetActiveConsoleSessionId();
        if (session_id == 0xFFFFFFFF) {
            LOGE("StartProcessInCurrentUser, WTSGetActiveConsoleSessionId failed");
            return false;
        }

        HANDLE user_token_raw{};  // NOLINT(gammaray-raw-pointer-boundary): WTSQueryUserToken out parameter, immediately RAII-wrapped.
        if (!WTSQueryUserToken(session_id, &user_token_raw)) {
            LOGE("StartProcessInCurrentUser, WTSQueryUserToken failed: {}", GetLastError());
            return false;
        }
        const UniqueWinHandle user_token{user_token_raw};

        HANDLE duplicate_raw{};  // NOLINT(gammaray-raw-pointer-boundary): DuplicateTokenEx out parameter, immediately RAII-wrapped.
        if (!DuplicateTokenEx(user_token.get(), TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &duplicate_raw)) {
            LOGE("StartProcessInCurrentUser, DuplicateTokenEx failed: {}", GetLastError());
            return false;
        }
        const UniqueWinHandle duplicate_token{duplicate_raw};

        void* environment_raw{};  // NOLINT(gammaray-raw-pointer-boundary): CreateEnvironmentBlock out parameter, immediately RAII-wrapped.
        if (!CreateEnvironmentBlock(&environment_raw, duplicate_token.get(), FALSE)) {
            LOGE("StartProcessInCurrentUser, CreateEnvironmentBlock failed: {}", GetLastError());
            return false;
        }
        const UniqueEnvironmentBlock environment{environment_raw};

        STARTUPINFOW si = { sizeof(si) };
        wchar_t desktop[] = L"WinSta0\\Default";
        si.lpDesktop = desktop;
        PROCESS_INFORMATION pi = { 0 };
        auto mutable_cmdline = cmdline;
        const BOOL success = CreateProcessAsUserW(
                duplicate_token.get(),
                nullptr,
                mutable_cmdline.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS,
                environment.get(),
                work_dir.c_str(),
                &si,
                &pi
        );
        if (!success) {
            LOGE("**CreateProcessAsUser failed, error: {:x}", GetLastError());
            return false;
        }
        const UniqueWinHandle process{pi.hProcess};
        const UniqueWinHandle thread{pi.hThread};
        if (wait) {
            WaitForSingleObject(process.get(), INFINITE);
        }
        return true;
    }

    uint32_t ProcessUtil::GetCurrentSessionId() {
        return GetProcessSessionId(GetCurrentProcessId());
    }

    uint32_t ProcessUtil::GetProcessSessionId(uint32_t pid)
    {
        DWORD sessionId{};
        if (ProcessIdToSessionId(pid, &sessionId))
            return sessionId;
        return UINT32_MAX;
    }

    int ProcessUtil::GetThreadCount() {
#ifdef WIN32
        DWORD threadCount = 0;
        const UniqueWinHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)};
        if (snapshot.get() == INVALID_HANDLE_VALUE) {
            LOGE("Failed to create snapshot!");
            return 0;
        }

        THREADENTRY32 te32{};
        te32.dwSize = sizeof(THREADENTRY32);
        DWORD currentPid = GetCurrentProcessId();

        if (Thread32First(snapshot.get(), &te32)) {
            do {
                if (te32.th32OwnerProcessID == currentPid) {
                    threadCount++;
                }
            } while (Thread32Next(snapshot.get(), &te32));
        }
        return threadCount;
#else
        int count = 0;
        for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task")) {
            if (entry.is_directory()) {
                auto name = entry.path().filename().string();
                if (name != "." && name != "..") {
                    count++;
                }
            }
        }
        return count;
#endif
    }

    void ProcessUtil::SetProcessInHighLevel() {
#ifdef WIN32
        const BOOL result = SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        if (!result) {
            LOGE("SetPriorityClass failed, err: {}", GetLastError());
        }
#endif
    }

    void ProcessUtil::PinToPerformanceCores() {
#ifdef WIN32
        // 混合架构 CPU(如 i7-13700KF 的 8P+8E):把进程亲和性钉到大核(P-core),
        // 避免采集/编码等延迟敏感线程被调度到小核(E-core)而增加延迟。
        // 通过 GetLogicalProcessorInformationEx 枚举物理核,取 EfficiencyClass 最大的
        // 那一组(性能核),构建亲和掩码后 SetProcessAffinityMask。单类 CPU 时等价于不限制。
        DWORD byte_length{};
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &byte_length) || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            LOGE("GetLogicalProcessorInformationEx size query failed, err: {}", GetLastError());
            return;
        }
        std::vector<std::byte> buffer(byte_length);
        if (!GetLogicalProcessorInformationEx(
                RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &byte_length)) {
            LOGE("GetLogicalProcessorInformationEx failed, err: {}", GetLastError());
            return;
        }

        // 找最大的 EfficiencyClass(性能核)。注意:实测 i7-13700KF 上小核 EfficiencyClass 更小,
        // 大核更大,所以取最大值;单类 CPU 所有核同值,等价于不限制。
        BYTE max_class{};
        for (std::size_t offset = 0; offset < byte_length;) {
            const auto& info = *reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
            if (info.Relationship == RelationProcessorCore && info.Processor.EfficiencyClass > max_class) {
                max_class = info.Processor.EfficiencyClass;
            }
            offset += info.Size;
        }

        // 收集所有属于性能核的逻辑处理器,构建亲和掩码(仅 group 0)
        DWORD_PTR mask{};
        for (std::size_t offset = 0; offset < byte_length;) {
            const auto& info = *reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
            if (info.Relationship == RelationProcessorCore && info.Processor.EfficiencyClass == max_class) {
                for (WORD group_index = 0; group_index < info.Processor.GroupCount; ++group_index) {
                    if (info.Processor.GroupMask[group_index].Group == 0) {
                        mask |= info.Processor.GroupMask[group_index].Mask;
                    }
                }
            }
            offset += info.Size;
        }

        if (mask == 0) {
            return;
        }
        if (SetProcessAffinityMask(GetCurrentProcess(), mask)) {
            LOGI("Pin process to performance cores, affinity mask: 0x{:x}", (uint64_t)mask);
        } else {
            LOGE("SetProcessAffinityMask failed, err: {}", GetLastError());
        }
#endif
    }

    bool ProcessUtil::RunAsAdminWithShell(const std::wstring& exePath, const std::wstring& parameters)
    {
        SHELLEXECUTEINFOW sei{sizeof(sei)};

        sei.lpVerb = L"runas";
        sei.lpFile = exePath.c_str();
        sei.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
        sei.nShow = SW_SHOWNORMAL;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;

        if (!ShellExecuteEx(&sei)) {
            DWORD err = GetLastError();
            if (err == ERROR_CANCELLED) {
                LOGE("UAC denied");
            } else {
                LOGE("UAC error: {}", err);
            }
            return false;
        }

        const UniqueWinHandle process{sei.hProcess};
        return true;
    }

}
#endif
