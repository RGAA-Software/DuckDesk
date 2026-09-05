#include "process_cmdline.h"

#include <cstddef>
#include <memory>

#include <winternl.h>

namespace px {

std::optional<std::wstring> ReadProcessCommandLine(HANDLE process) { // NOLINT(gammaray-raw-pointer-boundary): borrowed Win32 handle.
    if (process == nullptr || process == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    PROCESS_BASIC_INFORMATION basic_info{};
    if (NtQueryInformationProcess(process, ProcessBasicInformation, std::addressof(basic_info), sizeof(basic_info), nullptr) != 0) {
        return std::nullopt;
    }

    PEB peb{};
    SIZE_T bytes_read{};
    if (!ReadProcessMemory(process, basic_info.PebBaseAddress, std::addressof(peb), sizeof(peb), std::addressof(bytes_read)) ||
        bytes_read != sizeof(peb) || peb.ProcessParameters == nullptr) {
        return std::nullopt;
    }

    RTL_USER_PROCESS_PARAMETERS parameters{};
    if (!ReadProcessMemory(process, peb.ProcessParameters, std::addressof(parameters), sizeof(parameters), std::addressof(bytes_read)) ||
        bytes_read != sizeof(parameters)) {
        return std::nullopt;
    }

    const auto byte_count = static_cast<std::size_t>(parameters.CommandLine.Length);
    if (byte_count == 0) {
        return std::wstring{};
    }
    if (parameters.CommandLine.Buffer == nullptr || byte_count % sizeof(wchar_t) != 0) {
        return std::nullopt;
    }

    std::wstring command_line(byte_count / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(process, parameters.CommandLine.Buffer, command_line.data(), byte_count, std::addressof(bytes_read)) ||
        bytes_read > byte_count || bytes_read % sizeof(wchar_t) != 0) {
        return std::nullopt;
    }
    command_line.resize(static_cast<std::size_t>(bytes_read) / sizeof(wchar_t));
    return command_line;
}

} // namespace px
