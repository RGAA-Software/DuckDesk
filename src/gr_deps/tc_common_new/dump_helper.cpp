#include "dump_helper.h"
#include "log.h"
#include "file_util.h"
#include "string_util.h"
#include "time_util.h"
#include "folder_util.h"

#ifdef WIN32

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

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
#include "folder_util.h"
#include "client/windows/handler/exception_handler.h"
#include "client/windows/crash_generation/crash_generation_client.h"

#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "ntdll.lib")

namespace tc
{
    typedef BOOL (WINAPI *FuncMiniDumpWriteDump)(
            IN HANDLE hProcess,
            IN DWORD ProcessId,
            IN HANDLE hFile,
            IN MINIDUMP_TYPE DumpType,
            IN CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam, OPTIONAL
            IN CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam, OPTIONAL
            IN CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam OPTIONAL
    );


    LONG __stdcall UnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo) {
        MINIDUMP_EXCEPTION_INFORMATION ExpParam;
        HANDLE hCurrentProcess = GetCurrentProcess();
        wchar_t szFileName[MAX_PATH] = {0};
        GetModuleFileNameExW(hCurrentProcess, NULL, szFileName, _countof(szFileName));
        wchar_t* szDumpFileName = PathFindFileNameW(szFileName);
        PathRemoveExtensionW(szDumpFileName);
        PathAddExtensionW(szDumpFileName, L".dmp");
        wchar_t szPath[MAX_PATH] = {0};
        GetModuleFileNameExW(hCurrentProcess, NULL, szPath, _countof(szPath));
        PathRemoveFileSpecW(szPath);
        PathAppendW(szPath, L"dmp");
        SHCreateDirectoryExW(NULL, szPath, NULL);
        PathAppendW(szPath, szDumpFileName);

        HANDLE hFile = CreateFileW(szPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0,
                                  CREATE_ALWAYS, 0, 0);
        if (hFile != INVALID_HANDLE_VALUE) {
            ExpParam.ThreadId = GetCurrentThreadId();
            ExpParam.ExceptionPointers = pExceptionInfo;
            ExpParam.ClientPointers = TRUE;

            HMODULE hModule = LoadLibraryA("dbghelp.dll");
            if (hModule) {
                FuncMiniDumpWriteDump dumpWriteFunc = NULL;
                dumpWriteFunc = (FuncMiniDumpWriteDump) GetProcAddress(hModule, "MiniDumpWriteDump");
                if (dumpWriteFunc) {
                    dumpWriteFunc(hCurrentProcess, GetCurrentProcessId(), hFile, MiniDumpWithFullMemory, &ExpParam,
                                  NULL, NULL);
                }
            }
            CloseHandle(hFile);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void CaptureDump() {
        SetErrorMode(
                SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX | SEM_NOALIGNMENTFAULTEXCEPT | SEM_FAILCRITICALERRORS);
        SetUnhandledExceptionFilter(UnhandledExceptionFilter);
    }
}
#endif

#ifdef WIN32

namespace tc
{

    static bool DumpCallback(const wchar_t *dump_path, const wchar_t *minidump_id,
                             void *context, EXCEPTION_POINTERS *exinfo,
                             MDRawAssertionInfo *assertion, bool succeeded) {
        LOGE("!!!--CRASHED--!!");
        auto bc = (BreakpadContext*)context;
        if (succeeded) {
            auto exe_name = StringUtil::ToWString(bc->app_name_);
            auto exe_version = StringUtil::ToWString(bc->version_);
            std::wstring original_dmp = std::wstring(dump_path) + L"/" + minidump_id + L".dmp";
            std::wstring new_dmp_name = std::wstring(dump_path) + L"/" + exe_name + L"_" + exe_version + L".dmp";
            LOGE("ORIGIN: {}", StringUtil::ToUTF8(original_dmp));
            LOGE("TARGET: {}", StringUtil::ToUTF8(new_dmp_name));
            FileUtil::ReName(original_dmp, new_dmp_name);
            LOGE("!!!--DUMP Generated--!!");
        } else {
            LOGE("!!!--Can't Generate DUMP--!!");
        }

        return succeeded;
    }

    google_breakpad::ExceptionHandler *exception_handler = nullptr;

    void CaptureDumpByBreakpad(BreakpadContext* bc) {
        auto dump_path = FolderUtil::GetProgramDataPath() + L"/gr_dumps";
        FolderUtil::CreateDir(dump_path);

        exception_handler = new google_breakpad::ExceptionHandler(
            dump_path,
            nullptr,
            DumpCallback,
            bc,
            google_breakpad::ExceptionHandler::HANDLER_ALL
        );
    }

    void ClearOldDumps() {
        auto dump_path = FolderUtil::GetProgramDataPath() + L"/gr_dumps";
        CleanupDirectory(dump_path, 20);
    }

    void CleanupDirectory(const fs::path& dir, std::size_t keep_count) {
        if (!fs::exists(dir) || !fs::is_directory(dir)) {
            std::cerr << "Directory not exists: " << dir << std::endl;
            return;
        }

        struct FileInfo {
            fs::path path;
            fs::file_time_type time;
        };

        std::vector<FileInfo> files;

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                files.push_back({ entry.path(), entry.last_write_time() });
            }
        }

        if (files.size() <= keep_count) {
            return;
        }

        //按时间排序（新 → 旧）
        std::sort(files.begin(), files.end(),
            [](const FileInfo& a, const FileInfo& b) {
                return a.time > b.time;
            });

        // 删除多余的
        for (std::size_t i = keep_count; i < files.size(); ++i) {
            try {
                fs::remove(files[i].path);
                std::cout << "Deleted: " << files[i].path << std::endl;
            }
            catch (const std::exception& e) {
                std::cerr << "Failed to delete " << files[i].path
                    << ": " << e.what() << std::endl;
            }
        }
    }
}

#endif