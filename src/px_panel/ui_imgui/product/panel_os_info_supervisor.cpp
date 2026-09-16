#include "panel_os_info_supervisor.h"

#include "px_base/px_exe_names.h"
#include "px_common/log.h"

#include <Windows.h>

#include <chrono>
#include <string>
#include <utility>

namespace px::panel::product {

struct OsInfoLaunchState final {
    std::filesystem::path executable{};
    std::filesystem::path workingDirectory{};
    int panelPort{};
};

namespace {

class ProcessHandle final {
  public:
    explicit ProcessHandle(const HANDLE value) : value_{value} {}
    ~ProcessHandle() {
        if (Valid())
            CloseHandle(value_);
    }

    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

    [[nodiscard]] bool Valid() const noexcept {
        return value_ && value_ != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE Get() const noexcept {
        return value_;
    }

  private:
    HANDLE value_{};
};

void RunSupervisor(const std::shared_ptr<const OsInfoLaunchState>& state, const std::stop_token stopToken) {
    constexpr auto restartDelay{std::chrono::seconds{2}};
    while (!stopToken.stop_requested()) {
        std::wstring commandLine{L"\"" + state->executable.wstring() + L"\" --port=" + std::to_wstring(state->panelPort)};
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessW(state->executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                            nullptr, state->workingDirectory.c_str(), &startup, &processInfo)) {
            LOGE("Client px_osinfo failed to start: {}", GetLastError());
        } else {
            CloseHandle(processInfo.hThread);
            ProcessHandle process{processInfo.hProcess};
            LOGI("Client px_osinfo started, pid={}, panel_port={}", processInfo.dwProcessId, state->panelPort);
            while (!stopToken.stop_requested()) {
                const DWORD waitResult{WaitForSingleObject(process.Get(), 500)};
                if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_FAILED)
                    break;
            }
            if (stopToken.stop_requested() && process.Valid()) {
                static_cast<void>(TerminateProcess(process.Get(), 0));
                static_cast<void>(WaitForSingleObject(process.Get(), 2000));
                break;
            }
            DWORD exitCode{};
            static_cast<void>(GetExitCodeProcess(process.Get(), &exitCode));
            LOGW("Client px_osinfo exited unexpectedly with code {}; restarting", exitCode);
        }

        for (int elapsed{}; elapsed < restartDelay.count() * 10 && !stopToken.stop_requested(); ++elapsed)
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
}

} // namespace

std::shared_ptr<PanelOsInfoSupervisor> PanelOsInfoSupervisor::Create(const std::filesystem::path& executableDirectory, const int panelPort) {
    const auto executable = executableDirectory / px::kPxOsInfoExeName;
    if (panelPort <= 0 || panelPort > 65535 || !std::filesystem::is_regular_file(executable)) {
        LOGE("Client px_osinfo runtime is unavailable: path={}, panel_port={}", executable.string(), panelPort);
        return {};
    }
    const auto state = std::make_shared<const OsInfoLaunchState>(OsInfoLaunchState{
        .executable = executable,
        .workingDirectory = executableDirectory,
        .panelPort = panelPort,
    });
    return std::make_shared<PanelOsInfoSupervisor>(state);
}

PanelOsInfoSupervisor::PanelOsInfoSupervisor(std::shared_ptr<const OsInfoLaunchState> state) : state_{std::move(state)} {
    worker_ = std::jthread{[state = state_](const std::stop_token stopToken) { RunSupervisor(state, stopToken); }};
}

PanelOsInfoSupervisor::~PanelOsInfoSupervisor() {
    Stop();
}

void PanelOsInfoSupervisor::Stop() {
    if (!worker_.joinable())
        return;
    worker_.request_stop();
    worker_.join();
}

} // namespace px::panel::product
