#include "../../app/win/game_process_identity.h"
#include "px_common/win32/unique_win_handle.h"
#include <vector>
#include <array>

// Short-lived, non-interactive fixture. Its only child inherits the launch Job.
int main() {
    const std::wstring command{GetCommandLineW()};
    if (command.find(L"--check-environment") != std::wstring::npos) {
        std::array<wchar_t, 128> value{};
        const auto size = GetEnvironmentVariableW(L"PIXELS_LAUNCH_ENV_FIXTURE", value.data(), static_cast<DWORD>(value.size()));
        return size && size < value.size() && std::wstring_view(value.data(), size) == L"child-only Unicode 中文" ? 0 : 7;
    }
    px::UniqueWinHandle child{};
    if (command.find(L"--spawn-child") != std::wstring::npos) {
        std::vector<wchar_t> buffer(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length || length >= buffer.size()) {
            return 1;
        }
        const std::filesystem::path executable{std::wstring(buffer.data(), length)};
        auto arguments = px::GameCommandLine(executable, L"--leaf");
        if (!arguments) {
            return 2;
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION information{};
        if (!CreateProcessW(executable.c_str(), arguments->data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                            &information)) {
            return 3;
        }
        child.reset(information.hProcess);
        const px::UniqueWinHandle thread{information.hThread};
    }
    Sleep(20000);
    return 0;
}
