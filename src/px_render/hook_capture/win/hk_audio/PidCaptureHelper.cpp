#include "PidCaptureHelper.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <filesystem>
#include <string>
#include <vector>

#include "px_common/log.h"
#include "px_common/string_util.h"

namespace px {
namespace {

std::filesystem::path ModuleDir() {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ModuleDir), &mod) ||
        !mod) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(mod, path, MAX_PATH)) {
        return {};
    }
    return std::filesystem::path(path).parent_path();
}

DWORD ParentPid(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD parent = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                parent = pe.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return parent;
}

}  // namespace

bool StartPidCaptureHelper(uint32_t pid, const std::wstring& wav_path, void** out_process) {
    if (out_process) {
        *out_process = nullptr;
    }
    if (pid == 0 || wav_path.empty()) {
        return false;
    }

    const auto exe = ModuleDir() / L"px_audio_pid_capture.exe";
    if (!std::filesystem::exists(exe)) {
        LOGE("PidCaptureHelper: missing {}", StringUtil::ToUTF8(exe.wstring()));
        return false;
    }

    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + std::to_wstring(pid) + L" \"" +
                       wav_path + L"\" 0";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    // Launch as child of our parent (injector/test), NOT of the game. Being inside
    // the game's process tree makes PROCESS_LOOPBACK of that tree capture silence.
    DWORD parent_pid = ParentPid(GetCurrentProcessId());
    HANDLE parent = nullptr;
    if (parent_pid) {
        parent = OpenProcess(PROCESS_CREATE_PROCESS, FALSE, parent_pid);
    }

    SIZE_T attr_size = 0;
    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    siex.StartupInfo.wShowWindow = SW_HIDE;

    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = nullptr;
    DWORD flags = CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
    bool use_parent = false;

    if (parent) {
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
        attr_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            HeapAlloc(GetProcessHeap(), 0, attr_size));
        if (attr_list && InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size)) {
            if (UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
                                          &parent, sizeof(parent), nullptr, nullptr)) {
                siex.lpAttributeList = attr_list;
                use_parent = true;
            }
        }
    }

    if (!use_parent) {
        flags = CREATE_NO_WINDOW;
        siex.StartupInfo.cb = sizeof(STARTUPINFOW);
        siex.lpAttributeList = nullptr;
        LOGI("PidCaptureHelper: no parent reparent (fallback CreateProcess)");
    }

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, flags, nullptr,
                             exe.parent_path().c_str(), &siex.StartupInfo, &pi);

    if (attr_list) {
        DeleteProcThreadAttributeList(attr_list);
        HeapFree(GetProcessHeap(), 0, attr_list);
    }
    if (parent) {
        CloseHandle(parent);
    }

    if (!ok) {
        LOGE("PidCaptureHelper: CreateProcess failed err={}", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    if (out_process) {
        *out_process = pi.hProcess;
    } else {
        CloseHandle(pi.hProcess);
    }
    LOGI("PidCaptureHelper: started target_pid={} helper_pid={} reparent={} out={}", pid,
         pi.dwProcessId, use_parent, StringUtil::ToUTF8(wav_path));
    return true;
}

void StopPidCaptureHelper(void* process) {
    if (!process) {
        return;
    }
    auto* h = static_cast<HANDLE>(process);
    if (WaitForSingleObject(h, 500) == WAIT_TIMEOUT) {
        TerminateProcess(h, 0);
        WaitForSingleObject(h, 2000);
    }
    CloseHandle(h);
}

}  // namespace px
