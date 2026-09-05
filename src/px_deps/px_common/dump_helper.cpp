#include "dump_helper.h"
#include "log.h"
#include "file_util.h"
#include "string_util.h"
#include "time_util.h"
#include "folder_util.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <type_traits>
#include <vector>

#ifdef WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

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

namespace px {
namespace {
struct WinHandleCloser final {
    void operator()(void* handle) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): opaque Win32 HANDLE boundary.
        if (handle && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};

struct ModuleCloser final {
    void operator()(std::remove_pointer_t<HMODULE>* module) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): HMODULE ABI.
        if (module)
            FreeLibrary(module);
    }
};

using UniqueWinHandle = std::unique_ptr<void, WinHandleCloser>;
using UniqueModule = std::unique_ptr<std::remove_pointer_t<HMODULE>, ModuleCloser>;
} // namespace

using FuncMiniDumpWriteDump = decltype(&MiniDumpWriteDump); // NOLINT(gammaray-raw-pointer-boundary): dynamic Win32 procedure ABI.

LONG __stdcall UnhandledExceptionFilter(PEXCEPTION_POINTERS exception_info) { // NOLINT(gammaray-raw-pointer-boundary): Win32 callback ABI.
    const auto current_process = GetCurrentProcess();
    std::array<wchar_t, 32768> executable_name{};
    if (GetModuleFileNameExW(current_process, nullptr, executable_name.data(), static_cast<DWORD>(executable_name.size())) == 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    auto executable_path = std::filesystem::path{executable_name.data()};
    auto dump_directory = executable_path.parent_path() / L"dmp";
    std::error_code ignored{};
    std::filesystem::create_directories(dump_directory, ignored);
    auto dump_path = dump_directory / executable_path.filename().replace_extension(L".dmp");

    const UniqueWinHandle dump_file{
        CreateFileW(dump_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr)};
    if (dump_file && dump_file.get() != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception_parameters{};
        exception_parameters.ThreadId = GetCurrentThreadId();
        exception_parameters.ExceptionPointers = exception_info;
        exception_parameters.ClientPointers = TRUE;

        const UniqueModule module{LoadLibraryW(L"dbghelp.dll")};
        if (module) {
            const auto dump_write = reinterpret_cast<FuncMiniDumpWriteDump>(
                GetProcAddress(module.get(), "MiniDumpWriteDump")); // NOLINT(gammaray-raw-pointer-boundary): dynamic Win32 procedure ABI.
            if (dump_write) {
                static_cast<void>(dump_write(current_process, GetCurrentProcessId(), dump_file.get(), MiniDumpWithFullMemory,
                                             std::addressof(exception_parameters), nullptr, nullptr));
            }
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void CaptureDump() {
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX | SEM_NOALIGNMENTFAULTEXCEPT | SEM_FAILCRITICALERRORS);
    SetUnhandledExceptionFilter(UnhandledExceptionFilter);
}
} // namespace px
#endif

#ifdef WIN32

namespace px {

static bool DumpCallback(const wchar_t* dump_path,   // NOLINT(gammaray-raw-pointer-boundary): Breakpad callback ABI.
                         const wchar_t* minidump_id, // NOLINT(gammaray-raw-pointer-boundary): Breakpad callback ABI.
                         void* context,              // NOLINT(gammaray-raw-pointer-boundary): Breakpad callback ABI.
                         EXCEPTION_POINTERS*,        // NOLINT(gammaray-raw-pointer-boundary): Breakpad callback ABI.
                         MDRawAssertionInfo*,        // NOLINT(gammaray-raw-pointer-boundary): Breakpad callback ABI.
                         bool succeeded) {
    LOGE("event=crash.detected component=breakpad outcome=failed");
    const auto& breakpad_context = *static_cast<const BreakpadContext*>(context); // NOLINT(gammaray-raw-pointer-boundary): callback ABI.
    if (succeeded) {
        auto exe_name = StringUtil::ToWString(breakpad_context.app_name_);
        auto exe_version = StringUtil::ToWString(breakpad_context.version_);
        std::wstring original_dmp = std::wstring(dump_path) + L"/" + minidump_id + L".dmp";
        std::wstring new_dmp_name = std::wstring(dump_path) + L"/" + exe_name + L"_" + exe_version + L".dmp";
        static_cast<void>(FileUtil::ReName(original_dmp, new_dmp_name));
        LOGE("event=crash.dump component=breakpad operation=write outcome=succeeded path={}", StringUtil::ToUTF8(new_dmp_name));
    } else {
        LOGE("event=crash.dump component=breakpad operation=write outcome=failed retryable=false");
    }

    return succeeded;
}

class BreakpadRegistration::State final {
  public:
    explicit State(std::shared_ptr<const BreakpadContext> context_value) : context(std::move(context_value)) {
        FolderUtil::CreateDir(dump_path);
        handler = std::make_unique<google_breakpad::ExceptionHandler>(
            dump_path, nullptr, DumpCallback,
            const_cast<BreakpadContext*>(context.get()), // NOLINT(gammaray-raw-pointer-boundary): Breakpad context ABI.
            google_breakpad::ExceptionHandler::HANDLER_ALL);
    }

    std::shared_ptr<const BreakpadContext> context;
    std::wstring dump_path = FolderUtil::GetProgramDataPath() + L"/px_dumps";
    std::unique_ptr<google_breakpad::ExceptionHandler> handler;
};

BreakpadRegistration::BreakpadRegistration(std::shared_ptr<const BreakpadContext> context) : state_(std::make_unique<State>(std::move(context))) {}

BreakpadRegistration::~BreakpadRegistration() = default;

std::shared_ptr<BreakpadRegistration> CaptureDumpByBreakpad(std::shared_ptr<const BreakpadContext> context) {
    if (!context)
        return {};
    return std::make_shared<BreakpadRegistration>(std::move(context));
}

void ClearOldDumps() {
    auto dump_path = FolderUtil::GetProgramDataPath() + L"/px_dumps";
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
            files.push_back({entry.path(), entry.last_write_time()});
        }
    }

    if (files.size() <= keep_count) {
        return;
    }

    // 按时间排序（新 → 旧）
    std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) { return a.time > b.time; });

    // 删除多余的
    for (std::size_t i = keep_count; i < files.size(); ++i) {
        try {
            fs::remove(files[i].path);
            std::cout << "Deleted: " << files[i].path << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to delete " << files[i].path << ": " << e.what() << std::endl;
        }
    }
}
} // namespace px

#endif
