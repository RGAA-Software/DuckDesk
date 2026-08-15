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
#include <vector>
#include <WtsApi32.h>
#include "px_common_new/string_util.h"

#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "ntdll.lib")

constexpr auto kInjector32 = "";
constexpr auto kInjector64 = "";

constexpr auto kMaxTexBufSize = 1024;

namespace tc
{

    Response<bool, bool>
    WinHelper::IsDllInjected(uint32_t pid, const std::string &x86_dll_name, const std::string &x64_dll_name) {
        auto resp = Response<bool, bool>::Make(false, false);
        HANDLE hnd_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hnd_process == nullptr) {
            LOGE("IsAlreadyInject OpenProcess failed.");
            return resp;
        }

        BOOL x86;
        if (!IsWow64Process(hnd_process, &x86)) {
            LOGE("IsWow64Process failed with: {}", GetLastError());
            CloseHandle(hnd_process);
            return resp;
        }

        // 没有配置对应位数的 DLL 名（当前只有 64 位版 tc_graphics.dll），
        // 32 位目标直接视为未注入，避免空串比较导致的无效全量枚举
        if ((x86 && x86_dll_name.empty()) || (!x86 && x64_dll_name.empty())) {
            CloseHandle(hnd_process);
            resp.ok_ = true;
            resp.value_ = false;
            return resp;
        }

        bool ret_val = false;
        HMODULE h_modules[1024];
        DWORD needed = sizeof(h_modules);
        if (EnumProcessModulesEx(hnd_process, h_modules, needed,
                                 &needed, x86 ? LIST_MODULES_32BIT : LIST_MODULES_64BIT)) {
            for (int i = 0; i < needed / sizeof(HMODULE); ++i) {
                char name[MAX_PATH] = {0};
                if (GetModuleBaseNameA(hnd_process, h_modules[i], name, sizeof(name))) {
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

        CloseHandle(hnd_process);

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
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            LOGE("CreateProcessW failed: {}", GetLastError());
        }
        return resp;
    }

    Response<bool, bool> WinHelper::IsX86Arch(uint32_t pid) {
        auto resp = Response<bool, bool>::Make(false, false);
        HANDLE hnd_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hnd_process == nullptr) {
            return resp;
        }
        BOOL px86;
        if (!IsWow64Process(hnd_process, &px86)) {
            CloseHandle(hnd_process);
            return resp;
        }
        CloseHandle(hnd_process);
        resp.ok_ = true;
        resp.value_ = px86;
        return resp;
    }

    std::string WinHelper::GetExeFolderPath() {
        wchar_t file_path[MAX_PATH + 1] = {0};
        GetModuleFileNameW(nullptr, file_path, MAX_PATH);
        wchar_t* last_slash = wcsrchr(file_path, L'\\');
        if (last_slash) {
            *last_slash = L'\0';
        }
        return StringUtil::ToUTF8(file_path);
    }

    Response<bool, std::string> WinHelper::GetPathByHwnd(HWND hwnd) {
        auto ret = Response<bool, std::string>::Make(false, "");
        DWORD dwPid = -1;
        GetWindowThreadProcessId(hwnd, &dwPid);
        if (dwPid == -1)
            return ret;

        HANDLE handle = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                FALSE, dwPid);
        if (handle == NULL)
            return ret;
        std::shared_ptr<void> hprocessAutoFree(NULL, [=](void *) {
            CloseHandle(handle);
        });

        wchar_t path[4096] = {0};
        DWORD len = sizeof(path) / sizeof(*path);
        if (!QueryFullProcessImageNameW(handle, 0, path, &len)) {
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
        HANDLE handle = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                FALSE, pid);
        if (handle == NULL) {
            return ret;
        }
        std::shared_ptr<void> dtor(NULL, [=](void *) {
            CloseHandle(handle);
        });
        wchar_t path[4096] = {0};
        DWORD len = sizeof(path) / sizeof(*path);
        std::string upath;
        if (!QueryFullProcessImageNameW(handle, 0, path, &len)) {
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
        typedef HRESULT* (__stdcall* SetProcessDpiAwarenessFunc)(DPI_AWARENESS_CONTEXT);
        bool res = true;
        HMODULE shCore = LoadLibraryA("User32.dll");
        if (shCore) {
            auto setProcessDpiAwareness = (SetProcessDpiAwarenessFunc)GetProcAddress(shCore, "SetProcessDpiAwarenessContext");
            if (setProcessDpiAwareness != nullptr)
            {
                setProcessDpiAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            }
            else {
                res = false;
            }
            FreeLibrary(shCore);
        }
        return res;
    }


    bool WinHelper::InputDesktopSelected() {
        HDESK current = GetThreadDesktop(GetCurrentThreadId());
        HDESK input = OpenInputDesktop(0, FALSE,
            DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW |
            DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL |
            DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS |
            DESKTOP_SWITCHDESKTOP | GENERIC_WRITE);
        if (!input)
        {
            return FALSE;
        }

        DWORD size;
        char currentname[256];
        char inputname[256];

        if (!GetUserObjectInformation(current, UOI_NAME, currentname, sizeof(currentname), &size))
        {
            CloseDesktop(input);
            return FALSE;
        }
        if (!GetUserObjectInformation(input, UOI_NAME, inputname, sizeof(inputname), &size))
        {
            CloseDesktop(input);
            return FALSE;
        }
        CloseDesktop(input);
        
        return strcmp(currentname, inputname) == 0 ? TRUE : FALSE;
    }

    
    bool WinHelper::SelectInputDesktop() {
    
        // - Open the input desktop
        HDESK desktop = OpenInputDesktop(0, FALSE,
            DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW |
            DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL |
            DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS |
            DESKTOP_SWITCHDESKTOP | GENERIC_WRITE);
        if (!desktop)
        {
            return false;
        }

        // - Switch into it
        if (!SwitchToDesktop(desktop))
        {
            CloseDesktop(desktop);
            return false;
        }

        // ***
        DWORD size = 256;
        char currentname[256];
        if (GetUserObjectInformation(desktop, UOI_NAME, currentname, 256, &size))
        {
            //
        }

        return true;
    }

    bool WinHelper::SwitchToDesktop(HDESK desktop) {
        HDESK old_desktop = GetThreadDesktop(GetCurrentThreadId());
        if (!SetThreadDesktop(desktop))
        {
            return false;
        }
        if (!CloseDesktop(old_desktop))
        {
            //
        }
        return true;
    }

    bool WinHelper::IsSessionLocked() {
        DWORD sessionId = WTSGetActiveConsoleSessionId();
        LPTSTR buffer = nullptr;
        DWORD bytesReturned = 0;
        if (WTSQuerySessionInformation(
            WTS_CURRENT_SERVER_HANDLE,
            sessionId,
            WTSSessionInfoEx,
            &buffer,
            &bytesReturned)) {
            WTSINFOEX* info = (WTSINFOEX*)buffer;
            bool locked = false;
            if (info->Level == 1) {
                locked = (info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK);
            }
            WTSFreeMemory(buffer);
            return locked;
        }

        return false;
    }

}
