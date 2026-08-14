//
// Created by RGAA on 2024-02-05.
//

#include "process_helper.h"

#include <shellapi.h>
#include <DbgHelp.h>
#include <Psapi.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <shlobj_core.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <winternl.h>
#include <userenv.h>

#include "process_cmdline.h"
#include "tc_common_new/log.h"
#include "tc_common_new/file_util.h"
#include "tc_common_new/string_util.h"

#pragma comment(lib, "ntdll.lib")

namespace tc
{

    RespBoolBool ProcessHelper::IsProcessX86Arch(uint32_t pid) {
        auto ret = RespBoolBool::Make(false, false);
        HANDLE handle = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                FALSE, pid);
        if (handle == nullptr) {
            return ret;
        }
        BOOL px86;
        if (!IsWow64Process(handle, &px86)) {
            CloseHandle(handle);
            return ret;
        }
        CloseHandle(handle);
        ret.ok_ = true;
        ret.value_ = px86;
        return ret;
    }

    std::vector<ProcessInfoPtr> ProcessHelper::GetProcessList(bool query_icon) {
        std::vector<ProcessInfoPtr> processes;
        std::vector<ProcessInfoPtr> ret_list;

        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(pe32);

        HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hProcessSnap == INVALID_HANDLE_VALUE) {
            LOGE("CreateToolhelp32Snapshot Error!");
            return ret_list;
        }
        BOOL bResult = Process32FirstW(hProcessSnap, &pe32);
        while (bResult) {
            std::wstring curName = std::wstring(pe32.szExeFile);
            auto info = std::make_shared<ProcessInfo>();
            info->pid_ = pe32.th32ProcessID;
            info->ppid_ = pe32.th32ParentProcessID;
            processes.push_back(info);
            bResult = Process32NextW(hProcessSnap, &pe32);
        }
        if (hProcessSnap) {
            CloseHandle(hProcessSnap);
        }

        for (const auto& info: processes) {
            uint32_t pid = info->pid_;
            HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,FALSE, pid);
            if (handle == nullptr) {
                continue;
            }
            std::shared_ptr<void> dtor(nullptr, [=](void *) {
                CloseHandle(handle);
            });
            BOOL px86;
            if (!IsWow64Process(handle, &px86)) {
                continue;
            }

            bool x86 = px86;
            wchar_t path[4096] = {0};
            DWORD len = sizeof(path) / sizeof(*path);
            std::string upath;
            if (!QueryFullProcessImageNameW(handle, 0, path, &len)) {
                //LOGW("QueryFullProcessImageNameW failed.");
                continue;
            }

            if (query_icon) {
                std::wstring exe_path_w = path;
                info->icon_ = ProcessHelper::QueryExeIcon(exe_path_w);
                if (!info->icon_) {
                    info->icon_ = ProcessHelper::GetFileIcon(".exe");
                }
            }

            upath = StringUtil::ToUTF8(path);
            if (upath.empty()) {
                continue;
            }
            info->exe_full_path_ = upath;
            info->is_x86_ = x86;

            // exe name
            info->exe_name_ = FileUtil::GetFileNameFromPath(upath);

            // command line
            SIZE_T nCommandLineSize = 0;
            if (GetProcessCommandLineW(handle, nullptr, 0, &nCommandLineSize)) {
                std::wstring cmdline;
                cmdline.resize(nCommandLineSize);
                memset(cmdline.data(), 0, cmdline.size()*2);
                if (GetProcessCommandLineW(handle, cmdline.data(), nCommandLineSize, &nCommandLineSize)) {
                    info->exe_cmdline_ = StringUtil::ToUTF8(cmdline);
                }
            }

            // session id
            DWORD session_id = -1;
            if (ProcessIdToSessionId(info->pid_, &session_id)) {
                info->session_id_ = (int32_t)session_id;
            }

            if (upath.starts_with(R"(C:\Windows\System32\)")
                /*other....*/) {
                continue;
            }
            ret_list.push_back(info);
        }
        return ret_list;
    }

    bool ProcessHelper::CloseProcess(DWORD pid) {
        HANDLE hProcess = ::OpenProcess(PROCESS_ALL_ACCESS, TRUE, pid);
        if (hProcess) {
            bool ret = ::TerminateProcess(hProcess, 0);
            if (!ret) {
                LOGE("kill process failed. {} ", GetLastError());
            } else {
                LOGI("process {} closed.", pid);
            }
            CloseHandle(hProcess);
            return ret;
        } else {
            LOGE("open process failed.");
            return false;
        }
    }

    void ProcessHelper::CloseProcessesByName(const std::string& process_name, uint32_t exclude_pid) {
        auto processes = ProcessHelper::GetProcessList(false);
        for (const auto& process : processes) {
            if (process->exe_name_ != process_name) {
                continue;
            }
            if (exclude_pid != 0 && process->pid_ == exclude_pid) {
                continue;
            }
            LOGI("Force close process: {}, pid={}", process_name, process->pid_);
            ProcessHelper::CloseProcess(process->pid_);
        }
    }

    Response<bool, uint32_t> ProcessHelper::GetParentPid(uint32_t pid) {
        LONG status;
        DWORD dwParentPID = 0;
        PROCESS_BASIC_INFORMATION pbi;
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        auto ret = Response<bool, uint32_t>::Make(false, 0);
        if (!hProcess) {
            return ret;
        }
        status = NtQueryInformationProcess(hProcess, ProcessBasicInformation, (PVOID) &pbi,
                                           sizeof(PROCESS_BASIC_INFORMATION), NULL);
        if (status == 0) {
            dwParentPID = reinterpret_cast<DWORD>(pbi.Reserved3);
        } else {
            dwParentPID = 0;
        }
        CloseHandle(hProcess);
        ret.ok_ = true;
        ret.value_ = dwParentPID;
        return ret;
    }

    bool ProcessHelper::isChildOf(uint32_t child, uint32_t parent) {
        // 发现 windows server 2022 上会在winlog.exe 和 WINLOGINUI.exe 中循环，这里改为只判断5层关系。
        int i = 5;
        auto pid = child;
        do {
            auto ppid_ret = GetParentPid(pid);
            if (ppid_ret.value_ == parent) {
                return true;
            }
            pid = ppid_ret.value_;
        } while (i--);
        return false;
    }

    std::vector<uint32_t> ProcessHelper::FindAllChildProcess(uint32_t pid, const std::string &excludeProcessName) {
        std::vector<uint32_t> retList;
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(pe32);
        HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hProcessSnap == INVALID_HANDLE_VALUE) {
            LOGD("CreateToolhelp32Snapshot Error!");
            return retList;
        }
        BOOL bResult = Process32FirstW(hProcessSnap, &pe32);
        while (bResult) {
            if (StringUtil::ToUTF8(pe32.szExeFile) != excludeProcessName) {
                if (isChildOf(pe32.th32ProcessID, pid))
                    retList.push_back(pe32.th32ProcessID);
            }
            bResult = Process32NextW(hProcessSnap, &pe32);
        }
        CloseHandle(hProcessSnap);
        return retList;
    }

    uint32_t ProcessHelper::GetCurrentProcessId() {
        return ::GetCurrentProcessId();
    }

    static bool GetProcessNameByPid(uint32_t pid, std::wstring &exe_name, std::wstring &exe_path) {
        HANDLE processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (processHandle == NULL) {
            return false;
        }

        exe_name.resize(MAX_PATH);
        auto len = GetModuleBaseNameW(processHandle, NULL, exe_name.data(), MAX_PATH);
        if (len == 0) {
            LOGI("Get base name failed, err: {}", GetLastError());
            return false;
        }

        exe_path.resize(MAX_PATH);
        GetModuleFileNameExW(processHandle, NULL, exe_path.data(), MAX_PATH);

        //GetProcessImageFileNameA(processHandle, tempProcName, MAX_PATH);
        CloseHandle(processHandle);
        return true;
    }

    WindowInfos ProcessHelper::GetWindowInfoByPid(DWORD pid, int filter_window_size) {
        WindowInfos infos;
        infos.pid = pid;
        infos.filter_window_size = filter_window_size;

        EnumWindows([](HWND hwnd, LPARAM lParam) {
            auto infos = reinterpret_cast<WindowInfos *>(lParam);

            DWORD find_pid;
            auto thread_id = GetWindowThreadProcessId(hwnd, &find_pid);
            if (find_pid == infos->pid) {

                WindowInfo window_info;
                window_info.thread_id = thread_id;

                auto title_length = GetWindowTextLengthW(hwnd);
                if (title_length > 0) {
                    title_length++;
                }
                std::wstring title(title_length, 0);
                GetWindowTextW(hwnd, title.data(), title_length);

                //std::string class_name;
                //class_name.resize(256);
                //memset((char*)class_name.data(), 0, class_name.size());
                char path[256] = {0};
                memset(path, 0, 256);
                GetClassNameA(hwnd, path, 256);
                //GetClassNameA(hwnd, (char*)class_name.data(), class_name.size());

                window_info.pid = find_pid;
                window_info.title = title;
                window_info.win_handle = hwnd;
                window_info.claxx = std::string(path);// class_name;

                // filter small window
                auto size = window_info.GetWindowSize();
                if (infos->filter_window_size > 0) {
                    if (size.first <= infos->filter_window_size
                        || size.second <= infos->filter_window_size) {
                        window_info.win_handle = nullptr;
                        return TRUE;
                    }
                }

                if ("ConsoleWindowClass" == window_info.claxx) {
                    LOGI("filter the window : ConsoleWindowClass");
                    return TRUE;
                }

                GetProcessNameByPid(find_pid, window_info.exe_name, window_info.exe_path);
                //LOG_INFO("pid : %d , find : %d, result : %d, title : %s, exe : %s , path : %s ", window_info.pid, find_pid, result, UNICODEtoGBK(title).c_str(), UNICODEtoGBK(window_info.exe_name).c_str(), UNICODEtoGBK(window_info.exe_path).c_str());
                infos->infos.push_back(window_info);
            }

            return TRUE;
        }, reinterpret_cast<LPARAM>(&infos));

        return infos;
    }

    bool ProcessHelper::GetWindowPositionByHwnd(HWND hwnd, RECT &rect) {
        if (!GetClientRect(hwnd, &rect)) {
            LOGE("GetClientRect failed: {}", GetLastError());
            return false;
        }

        {
            POINT point{rect.left, rect.top};
            if (!ClientToScreen(hwnd, &point)) {
                LOGE("ClientToScreen failed");
                return false;
            }
            rect.left = point.x;
            rect.top = point.y;
        }
        {
            POINT point{rect.right, rect.bottom};
            if (!ClientToScreen(hwnd, &point)) {
                LOGE("ClientToScreen failed 2");
                return false;
            }
            rect.right = point.x;
            rect.bottom = point.y;
        }

        return true;
    }


    HICON ProcessHelper::QueryExeIcon(const std::wstring& exe_path) {
        HICON hIcon = nullptr;
        SHFILEINFO FileInfo;
        DWORD_PTR dwRet = ::SHGetFileInfoW(exe_path.c_str(), 0, &FileInfo, sizeof(SHFILEINFO), SHGFI_ICON);
        if (dwRet) {
            hIcon = FileInfo.hIcon;
        }
        return hIcon;
    }

    HICON ProcessHelper::GetFolderIcon() {
        std::string str = "folder";
        LPCSTR name = str.c_str();
        HICON icon = nullptr;
        SHFILEINFOA info;
        if (SHGetFileInfoA(name,
                           FILE_ATTRIBUTE_DIRECTORY,
                           &info,
                           sizeof(info),
                           SHGFI_SYSICONINDEX | SHGFI_ICON | SHGFI_USEFILEATTRIBUTES))
        {
            icon = info.hIcon;
        }
        return icon;
    }

    HICON ProcessHelper::GetFileIcon(const std::string& suffix) {
        HICON icon = nullptr;
        if (!suffix.empty()) {
            LPCSTR name = suffix.c_str();
            SHFILEINFOA info;
            if (SHGetFileInfoA(name,
                               FILE_ATTRIBUTE_NORMAL,
                               &info,
                               sizeof(info),
                               SHGFI_SYSICONINDEX | SHGFI_ICON | SHGFI_USEFILEATTRIBUTES))
            {
                icon = info.hIcon;
            }
        }
        return icon;
    }

}
