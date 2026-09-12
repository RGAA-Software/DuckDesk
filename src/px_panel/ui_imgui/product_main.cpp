#include "panel_preview.h"
#include "product/panel_product_runtime.h"

#include "px_desktop_shell/desktop_shell.h"

#include "px_common/folder_util.h"
#include "px_common/auto_start.h"
#include "px_common/hardware.h"
#include "px_common/log.h"
#include "px_common/process_util.h"
#include "render_panel/px_running_pipe.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct CommandLineOptions final {
    bool runAutomatically{false};
    bool debug{false};
    std::string skinName{};
};

CommandLineOptions ParseCommandLine(const int argc, char* argv[]) { // NOLINT(gammaray-raw-pointer-boundary): process-entry ABI
    CommandLineOptions options{};
    for (int index{1}; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--run_automatically") {
            options.runAutomatically = true;
        } else if (argument == "--debug") {
            options.debug = true;
        } else if (argument.starts_with("--skin=")) {
            options.skinName = argument.substr(7);
        }
    }
    return options;
}

bool PrepareRuntimeDirectories(const std::filesystem::path& basePath) {
    constexpr std::array directories{"px_logs", "px_data", "px_data/client", "px_data/render", "px_data/panel", "px_data/cache", "px_dumps"};
    std::error_code error{};
    for (const auto directory : directories) {
        std::filesystem::create_directories(basePath / directory, error);
        if (error) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) { // NOLINT(gammaray-raw-pointer-boundary): process-entry ABI
    px::Hardware::AcquirePermissionForRestartDevice();
    px::ProcessUtil::SetProcessInHighLevel();
    const auto options = ParseCommandLine(argc, argv);
    const auto runningPipe = std::make_shared<px::PxRunningPipe>();
    if (runningPipe->SendHello()) {
        return 0;
    }
    const auto showRequested = std::make_shared<std::atomic_bool>(false);
    runningPipe->StartListening([showRequested] { showRequested->store(true, std::memory_order_release); });
    const std::filesystem::path basePath{px::FolderUtil::GetProgramDataPath()};
    if (!PrepareRuntimeDirectories(basePath)) {
        return 1;
    }
    px::Logger::InitLog((basePath / "px_logs" / "pixels.log").wstring(), true);
    auto shellResult = px::desktop::DesktopShell::Create(
        {.title = "Pixels", .width = 1180, .height = 760, .initiallyVisible = !options.runAutomatically, .minimizeToTray = true});
    if (!shellResult) {
        return 3;
    }

    int result{};
    {
        const auto shell = std::make_shared<px::desktop::DesktopShell>(std::move(shellResult.value()));
        const std::weak_ptr<px::desktop::DesktopShell> weakShell{shell};
        const auto autoStart = std::make_shared<px::AutoStart>();
        const auto notifications = std::make_shared<px::panel::ui::NotificationCenter>();
        const std::filesystem::path executablePath{std::filesystem::path{argv[0]}};
        const auto runtime = px::panel::product::PanelProductRuntime::Create(executablePath.parent_path(), notifications);
        if (!runtime) {
            return 4;
        }
        static_cast<void>(autoStart->CreateLogonTask("px_panel_start", executablePath, "--run_automatically", "GR"));
        const auto showPanel = [weakShell] {
            if (const auto activeShell = weakShell.lock())
                activeShell->RequestShowAndRaise();
        };
        px::panel::ui::PanelPreview panel{{
            .account = px::panel::product::CreateProductAccountPort(runtime),
            .notifications = notifications,
            .networkSettings = px::panel::product::CreateProductNetworkSettingsPort(runtime),
            .serverStatus = px::panel::product::CreateProductServerStatusPort(runtime),
            .remoteControl = px::panel::product::CreateProductRemoteControlPort(runtime),
            .cloudApplications = px::panel::product::CreateProductCloudApplicationsPort(runtime),
            .settings = px::panel::product::CreateProductSettingsPort(runtime),
            .securityRecords = px::panel::product::CreateProductSecurityRecordsPort(runtime),
            .voiceCallConsent = px::panel::product::CreateProductVoiceCallConsentOverlay(runtime, showPanel),
        }};
        result = shell->Run([&panel, weakShell, showRequested] {
            if (showRequested->exchange(false, std::memory_order_acq_rel)) {
                if (const auto activeShell = weakShell.lock()) {
                    activeShell->RequestShowAndRaise();
                }
            }
            const auto action = panel.Draw();
            if (action.selectedTheme.has_value()) {
                if (const auto activeShell = weakShell.lock()) {
                    activeShell->SetTheme(*action.selectedTheme);
                }
            }
            if (action.exitRequested) {
                if (const auto activeShell = weakShell.lock()) {
                    activeShell->RequestExit();
                }
            }
        });
    }
    LOGI("ImGui Panel shell exited, beginning business shutdown");
    runningPipe->StopListening();
    LOGI("ImGui Panel business shutdown completed");
    return result;
}
