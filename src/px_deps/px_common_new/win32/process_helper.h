//
// Created by RGAA on 2024-02-05.
//

#ifndef TC_APPLICATION_PROCESS_HELPER_H
#define TC_APPLICATION_PROCESS_HELPER_H

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

#include "px_common_new/response.h"
#include "px_common_new/win32/unique_win_handle.h"

namespace px
{

    class ProcessInfo {
    public:
        // C:\xx\xxx\xxx.exe
        std::string exe_full_path_{};
        std::string exe_name_{};
        std::string exe_cmdline_{};
        bool is_x86_{};
        uint32_t pid_{};
        uint32_t ppid_{};
        uint32_t thread_id_{};
        UniqueWinIcon icon_{};
        std::string icon_name_{};
        int32_t session_id_{};

        [[nodiscard]] bool Valid() const {
            return pid_ > 0 && !exe_full_path_.empty();
        }

        ~ProcessInfo() = default;
    };
    using ProcessInfoPtr = std::shared_ptr<ProcessInfo>;

    class WindowInfo {
    public:
        [[nodiscard]] std::pair<int, int> GetWindowSize() const {
            if (!win_handle) {
                return std::make_pair(0, 0);
            }
            RECT rect{};
            GetWindowRect(reinterpret_cast<HWND>(win_handle), &rect);
            return std::make_pair((rect.right - rect.left), (rect.bottom - rect.top));
        }

    public:
        DWORD pid{};
        DWORD thread_id{};
        std::uintptr_t win_handle{};
        std::wstring title{};
        std::wstring exe_name{};
        std::wstring exe_path{};
        std::string claxx{};
    };

    class WindowInfos {
    public:

        int pid{};
        int filter_window_size{-1};
        std::vector<WindowInfo> infos{};

    };

    class ProcessHelper {
    public:
        static RespBoolBool IsProcessX86Arch(uint32_t pid);
        static std::vector<std::shared_ptr<ProcessInfo>> GetProcessList(bool icon = false);
        static bool CloseProcess(DWORD pid);
        static void CloseProcessesByName(const std::string& process_name, uint32_t exclude_pid = 0);
        static Response<bool, uint32_t> GetParentPid(uint32_t pid);
        static bool isChildOf(uint32_t child, uint32_t parent);
        static std::vector<uint32_t> FindAllChildProcess(uint32_t pid, const std::string& excludeProcessName = "");
        static uint32_t GetCurrentProcessId();
        static WindowInfos GetWindowInfoByPid(DWORD pid, int filter_window_size = 256);
        static bool GetWindowPositionByHwnd(
            HWND hwnd,  // NOLINT(gammaray-raw-pointer-boundary): borrowed window handle used synchronously.
            RECT& rect);
        static UniqueWinIcon QueryExeIcon(const std::wstring& exe_path);
        static UniqueWinIcon GetFolderIcon();
        //std::string strArray[13] = {".exe", ".zip", ".har", ".hwl", ".accdb",
        //                            ".xlsx", ".pptx", ".docx", ".txt", ".h", ".cpp", ".pro"};
        static UniqueWinIcon GetFileIcon(const std::string& suffix);
    };

}

#endif //TC_APPLICATION_PROCESS_HELPER_H
