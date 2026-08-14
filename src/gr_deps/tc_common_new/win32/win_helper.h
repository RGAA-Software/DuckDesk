//
// Created by RGAA on 2024/2/2.
//

#ifndef TC_APPLICATION_WIN_HELPER_H
#define TC_APPLICATION_WIN_HELPER_H

#include <string>
#include <cstdint>

#include "tc_common_new/log.h"
#include <Windows.h>
#include "tc_common_new/response.h"

namespace tc
{

    class WinHelper {
    public:

        static Response<bool, bool> IsDllInjected(uint32_t pid, const std::string& x86_dll_name, const std::string& x64_dll_name);
        static Response<bool, bool> InjectDll(uint32_t pid, uint32_t tid, const std::string& x86_dll_name, const std::string& x64_dll_name);
        static Response<bool, bool> IsX86Arch(uint32_t pid);
        static Response<bool, std::string> GetPathByHwnd(HWND hwnd);
        static Response<bool, std::string> GetErrorStr(HRESULT hr);
        static Response<bool, std::string> GetExeName(DWORD pid);
        static Response<bool, std::string> GetModuleName(HMODULE hModule);
        static Response<bool, std::wstring> GetModulePathW(HMODULE hModule);
        static Response<bool, std::string> GetModulePath(HMODULE hModule);
        static Response<bool, std::string> Win32GetClassName(HWND hwnd);
        static Response<bool, std::string> Win32GetWindowTitle(HWND hwnd);
        static Response<bool, HWND> FindHwndByPid(uint32_t pid);
        static std::string GetExeFolderPath();
        static bool DontCareDPI();

        // Determine whether the thread's current desktop is the input one
        static bool InputDesktopSelected();

        // Switch the current thread into the input desktop
        static bool SelectInputDesktop();

        // Switch the current thread to the specified desktop
        static bool SwitchToDesktop(HDESK desktop);

        // is session locked
        static bool IsSessionLocked();
    };

}

#endif //TC_APPLICATION_WIN_HELPER_H
