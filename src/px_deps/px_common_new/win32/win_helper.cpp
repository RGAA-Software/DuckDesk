//
// Created by RGAA on 2024/2/2.
//

#include "win_helper.h"

#include <Windows.h>
#include <Psapi.h>
#include <Shlwapi.h>
#include <tchar.h>
#include <winternl.h>
#include <filesystem>
#include <format>
#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>
#include <WtsApi32.h>
#include "px_common_new/string_util.h"
#include "px_common_new/win32/unique_win_handle.h"

#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "ntdll.lib")

constexpr auto kInjector32 = "";
constexpr auto kInjector64 = "";

constexpr auto kMaxTexBufSize = 1024;

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

        struct DesktopCloser final {
            void operator()(std::remove_pointer_t<HDESK>* desktop) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): HDESK ABI.
                if (desktop != nullptr) {
                    CloseDesktop(desktop);
                }
            }
        };
        using UniqueDesktop = std::unique_ptr<std::remove_pointer_t<HDESK>, DesktopCloser>;

        struct WtsMemoryCloser final {
            void operator()(TCHAR* memory) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): WTS allocation ABI.
                WTSFreeMemory(memory);
            }
        };
        using UniqueWtsMemory = std::unique_ptr<TCHAR, WtsMemoryCloser>;
    }

    Response<bool, bool>
    WinHelper::IsDllInjected(uint32_t pid, const std::string &x86_dll_name, const std::string &x64_dll_name) {
        auto resp = Response<bool, bool>::Make(false, false);
        const UniqueWinHandle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid)};
        if (!process) {
            LOGE("IsAlreadyInject OpenProcess failed.");
            return resp;
        }

        BOOL x86{};
        if (!IsWow64Process(process.get(), &x86)) {
            LOGE("IsWow64Process failed with: {}", GetLastError());
            return resp;
        }

        // 没有配置对应位数的 DLL 名（当前只有 64 位版 px_gh.dll），
        // 32 位目标直接视为未注入，避免空串比较导致的无效全量枚举
        if ((x86 && x86_dll_name.empty()) || (!x86 && x64_dll_name.empty())) {
            resp.ok_ = true;
            resp.value_ = false;
            return resp;
        }

        bool ret_val = false;
        std::array<std::uintptr_t, 1024> module_values{};
        DWORD needed = sizeof(module_values);
        if (EnumProcessModulesEx(process.get(), reinterpret_cast<HMODULE*>(module_values.data()), needed,
                                 &needed, x86 ? LIST_MODULES_32BIT : LIST_MODULES_64BIT)) {
            const auto module_count = (std::min)(module_values.size(), static_cast<std::size_t>(needed) / sizeof(HMODULE));
            for (std::size_t i = 0; i < module_count; ++i) {
                char name[MAX_PATH]{};
                if (GetModuleBaseNameA(process.get(), reinterpret_cast<HMODULE>(module_values[i]), name, sizeof(name))) {
                    //_strlwr(name);
                    if (x86) {
                        if (::_stricmp(x86_dll_name.c_str(), name) == 0) {
                            ret_val = true;
                            break;
                        }
                    } else {
                        if (::_stricmp(x64_dll_name.c_str(), name) == 0) {
                            ret_val = true;
                            break;
                        }
                    }
                } else {
                    LOGE("GetModuleBaseNameA failed with: {}", GetLastError());
                }
            }
        } else {
            LOGE("EnumProcessModulesEx failed with: {}", GetLastError());
        }

        resp.ok_ = true;
        resp.value_ = ret_val;
        return resp;
    }

    Response<bool, bool>
    WinHelper::InjectDll(uint32_t pid, uint32_t tid, const std::string &x86_dll_name, const std::string &x64_dll_name) {
        auto resp = Response<bool, bool>::Make(false, false);
        auto is_x86 = IsX86Arch(pid);
        if (!is_x86.ok_) {
            LOGE("Check arch failed when InjectDll...");
            return resp;
        }

        std::string target_dll = is_x86.value_ ? x86_dll_name : x64_dll_name;
        std::string injector = is_x86.value_ ? kInjector32 : kInjector64;
        std::string cheat_anti = "0";
        // todo: Test it.
        std::wstring cmd_line = std::format(L"\"{}\" \"{}\" \"{}\" {}",
                                            StringUtil::ToWString(injector),
                                            StringUtil::ToWString(target_dll),
                                            StringUtil::ToWString(cheat_anti),
                                            pid);

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, &cmd_line[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            const UniqueWinHandle process{pi.hProcess};
            const UniqueWinHandle thread{pi.hThread};
            WaitForSingleObject(process.get(), INFINITE);
        } else {
            LOGE("CreateProcessW failed: {}", GetLastError());
        }
        return resp;
    }

    Response<bool, bool> WinHelper::IsX86Arch(uint32_t pid) {
        auto resp = Response<bool, bool>::Make(false, false);
        const UniqueWinHandle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid)};
        if (!process) {
            return resp;
        }
        BOOL px86{};
        if (!IsWow64Process(process.get(), &px86)) {
            return resp;
        }
        resp.ok_ = true;
        resp.value_ = px86;
        return resp;
    }

    std::string WinHelper::GetExeFolderPath() {
        std::wstring file_path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, file_path.data(), static_cast<DWORD>(file_path.size()));
        if (length == 0 || length == file_path.size()) {
            LOGE("GetModuleFileNameW failed: {}", GetLastError());
            return {};
        }
        file_path.resize(length);
        return StringUtil::ToUTF8(std::filesystem::path(file_path).parent_path().wstring());
    }

    Response<bool, std::string> WinHelper::GetPathByHwnd(HWND hwnd) {
        auto ret = Response<bool, std::string>::Make(false, "");
        DWORD dwPid{};
        GetWindowThreadProcessId(hwnd, &dwPid);
        if (dwPid == 0)
            return ret;

        const UniqueWinHandle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPid)};
        if (!process)
            return ret;

        wchar_t path[4096]{};
        DWORD len = std::size(path);
        if (!QueryFullProcessImageNameW(process.get(), 0, path, &len)) {
            LOGW("QueryFullProcessImageNameW failed.");
            return ret;
        }
        ret.ok_ = true;
        ret.value_ = StringUtil::ToUTF8(path);
        return ret;
    }

    Response<bool, std::string> WinHelper::GetErrorStr(HRESULT hr) {
        wchar_t buffer[4096] = {0};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, hr,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       buffer, sizeof(buffer) / sizeof(*buffer), NULL);
        return Response<bool, std::string>::Make(true, StringUtil::ToUTF8(buffer));
    }

    Response<bool, std::string> WinHelper::GetExeName(DWORD pid) {
        auto ret = Response<bool, std::string>::Make(false, "");
        const UniqueWinHandle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid)};
        if (!process) {
            return ret;
        }
        wchar_t path[4096]{};
        DWORD len = std::size(path);
        std::string upath;
        if (!QueryFullProcessImageNameW(process.get(), 0, path, &len)) {
            LOGW("QueryFullProcessImageNameW failed.");
            return ret;
        }
        upath = StringUtil::ToUTF8(path);
        if (upath.empty()) {
            return ret;
        }

        std::filesystem::path file_path(StringUtil::ToWString(upath));
        ret.ok_ = true;
        ret.value_ = StringUtil::ToUTF8(file_path.filename().wstring());
        return ret;
    }

    Response<bool, std::string> WinHelper::GetModuleName(HMODULE hModule) {
        const int maxPath = 4096;
        wchar_t szFullPath[maxPath] = { 0 };
        ::GetModuleFileNameW(hModule, szFullPath, maxPath);
        std::filesystem::path file_path(szFullPath);
        auto ret = Response<bool, std::string>::Make(false, "");
        if (file_path.filename().string().empty()) {
            return ret;
        }
        ret.ok_ = true;
        ret.value_ = StringUtil::ToUTF8(file_path.filename().wstring());
        return ret;
    }

    Response<bool, std::wstring> WinHelper::GetModulePathW(HMODULE hModule) {
        const int maxPath = 4096;
        wchar_t szFullPath[maxPath] = { 0 };
        ::GetModuleFileNameW(hModule, szFullPath, maxPath);
        ::PathRemoveFileSpecW(szFullPath);
        return Response<bool, std::wstring>::Make(true, szFullPath);
    }

    Response<bool, std::string> WinHelper::GetModulePath(HMODULE hModule) {
        auto file_path = StringUtil::ToUTF8(GetModulePathW(hModule).value_);
        return Response<bool, std::string>::Make(true, file_path);
    }

    Response<bool, std::string> WinHelper::Win32GetClassName(HWND hwnd) {
        auto ret = Response<bool, std::string>::Make(false, "");
        wchar_t clazzName[kMaxTexBufSize] = { 0 };
        if (GetClassNameW(hwnd, clazzName, kMaxTexBufSize) == 0) {
            LOGW("GetClassNameW failed with:%d", GetLastError());
            return ret;
        }
        ret.ok_ = true;
        ret.value_ = StringUtil::ToUTF8(clazzName);
        return ret;
    }

    Response<bool, std::string> WinHelper::Win32GetWindowTitle(HWND hwnd) {
        auto ret = Response<bool, std::string>::Make(false, "");
        wchar_t text[kMaxTexBufSize] = { 0 };
        if (GetWindowTextW(hwnd, text, kMaxTexBufSize) <= 0) {
            LOGI("GetWindowTextW hwnd {} failed with:{}",(void*)hwnd, GetLastError());
            return ret;
        }
        ret.ok_ = true;
        ret.value_ = StringUtil::ToUTF8(text);
        return ret;
    }

    Response<bool, HWND> WinHelper::FindHwndByPid(uint32_t pid) {
        auto ret = Response<bool, HWND>::Make(false, nullptr);
        HWND hWnd;
        DWORD dwProcessNowId;
        hWnd = GetTopWindow(NULL);
        while (hWnd) {
            GetWindowThreadProcessId(hWnd, &dwProcessNowId);
            if (dwProcessNowId == pid) {
                ret.ok_ = true;
                ret.value_ = hWnd;
                return ret;
            } else {
                hWnd = GetNextWindow(hWnd, GW_HWNDNEXT);
            }
        }
        return ret;
    }

    bool WinHelper::DontCareDPI() {
        using SetProcessDpiAwarenessFunc = BOOL(__stdcall*)(DPI_AWARENESS_CONTEXT);  // NOLINT(gammaray-raw-pointer-boundary): Win32 ABI.
        const UniqueModule user32{LoadLibraryW(L"User32.dll")};
        if (!user32) {
            return false;
        }
        const auto set_process_dpi_awareness = reinterpret_cast<SetProcessDpiAwarenessFunc>(
            GetProcAddress(user32.get(), "SetProcessDpiAwarenessContext"));
        return set_process_dpi_awareness != nullptr && set_process_dpi_awareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
    }


    bool WinHelper::InputDesktopSelected() {
        const HDESK current = GetThreadDesktop(GetCurrentThreadId());  // NOLINT(gammaray-raw-pointer-boundary): borrowed thread desktop.
        const UniqueDesktop input{OpenInputDesktop(0, FALSE,
            DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW |
            DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL |
            DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS |
            DESKTOP_SWITCHDESKTOP | GENERIC_WRITE)};
        if (!input)
        {
            return FALSE;
        }

        DWORD size{};
        char currentname[256]{};
        char inputname[256]{};

        if (!GetUserObjectInformation(current, UOI_NAME, currentname, sizeof(currentname), &size))
        {
            return FALSE;
        }
        if (!GetUserObjectInformation(input.get(), UOI_NAME, inputname, sizeof(inputname), &size))
        {
            return FALSE;
        }
        
        return strcmp(currentname, inputname) == 0 ? TRUE : FALSE;
    }

    
    bool WinHelper::SelectInputDesktop() {
    
        // - Open the input desktop
        UniqueDesktop desktop{OpenInputDesktop(0, FALSE,
            DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW |
            DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL |
            DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS |
            DESKTOP_SWITCHDESKTOP | GENERIC_WRITE)};
        if (!desktop)
        {
            return false;
        }

        // - Switch into it
        if (!SwitchToDesktop(desktop.get()))
        {
            return false;
        }

        // ***
        DWORD size = 256;
        char currentname[256]{};
        if (GetUserObjectInformation(desktop.get(), UOI_NAME, currentname, 256, &size))
        {
            //
        }

        thread_local UniqueDesktop selected_desktop;
        selected_desktop = std::move(desktop);
        return true;
    }

    bool WinHelper::SwitchToDesktop(HDESK desktop) {
        if (!SetThreadDesktop(desktop))
        {
            return false;
        }
        return true;
    }

    bool WinHelper::IsSessionLocked() {
        const DWORD sessionId = WTSGetActiveConsoleSessionId();
        LPTSTR buffer_raw{};  // NOLINT(gammaray-raw-pointer-boundary): WTS out parameter, immediately RAII-wrapped.
        DWORD bytesReturned{};
        if (WTSQuerySessionInformation(
            WTS_CURRENT_SERVER_HANDLE,
            sessionId,
            WTSSessionInfoEx,
            &buffer_raw,
            &bytesReturned)) {
            const UniqueWtsMemory buffer{buffer_raw};
            const auto& info = *reinterpret_cast<const WTSINFOEX*>(buffer.get());
            bool locked{};
            if (info.Level == 1) {
                locked = (info.Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK);
            }
            return locked;
        }

        return false;
    }

}
